// Scaredy Cat DSP core (pure C++17, no JUCE): drive > 3-band split > per-band volume-shaper envelopes > limiter > hidden soft clipper.
// Original code, AGPL-3.0-or-later. No allocation after prepare(), no locks, no I/O, no network.
//
// Signal path
//   input -> drive (2x oversampled, six mixable stages: Soft, Tape, Tube, PurrOne Drive, Transistor, Fuzz) ->
//   split in 3 bands (Linkwitz-Riley 12/24/48 dB, or linear phase) -> band gain from its envelope (depth = AMOUNT x band amount) -> sum ->
//   Loudness (LA-2A style opto + hidden soft clipper at the limiter ceiling, dry/wet) -> lookahead limiter -> mix with delayed dry.
//   AMOUNT = 0 is an exact bypass (the delayed input, bit for bit); 100 is the full envelope depth.
#pragma once
#include "ScaredyEnv.h"
#include "PurrColour.h"
#include <atomic>
#include <cstring>

namespace scaredy
{
constexpr double kPi = 3.14159265358979323846;

struct Params
{
    double amount = 100.0, amtLo = 100.0, amtMid = 100.0, amtHi = 100.0;   // percent
    double xLo = 120.0, xHi = 2400.0;                                       // crossover frequencies in Hz
    int slope = 1;                                                          // 0: 12, 1: 24, 2: 48 dB per octave
    bool multiband = true, linear = false, linked = false;
    bool sync = true; double cycleBeats = 1.0, hz = 2.0;                    // cycle length: beats (sync) or Hz
    int trigger = 0;                                                        // 0 repeat, 1 input hit
    bool scTrigger = false; double threshDb = -24.0;                        // hit detection (sidechain input when scTrigger)
    double swing = 0.0, smooth = 4.0;                                       // percent
    double offset = 0.0; bool offsetSamples = false;                        // milliseconds or samples (positive = later)
    double dTube = 0.0, dTape = 0.0, dSoft = 0.0, dDrive = 0.0, dTrans = 0.0, dFuzz = 0.0;   // percent, six drive stages in series
    double loud = 0.0;                                                      // percent: Loudness (opto + hidden clipper, dry/wet)
    bool limOn = true; double ceilDb = -0.3;
    int solo = 0;                                                           // 0 off, 1 low, 2 mid, 3 high
};
struct Timing { bool playing = false, havePpq = false; double ppq = 0.0, bpm = 120.0; };

inline int linTaps(int slope) { return slope <= 0 ? 513 : slope == 1 ? 1025 : 2049; }
inline int lookaheadFor(double fs) { return std::max(4, std::min(560, (int) std::ceil(0.0015 * fs))); }
constexpr int kOsLatency = 32;   // one 2x oversampling pair (up + down), in samples at the base rate
inline int latencyFor(double fs, bool linear, int slope) { return (linear ? (linTaps(slope) - 1) / 2 : 0) + 2 * kOsLatency + lookaheadFor(fs); }

// Magnitude of the Linkwitz-Riley low/high-pass at f for crossover fc (for the display)
inline double lrMag(double f, double fc, int slope, bool high)
{
    const double n = slope <= 0 ? 2.0 : slope == 1 ? 4.0 : 8.0, x = std::pow(std::max(1e-9, f / fc), n);
    return high ? x / (1.0 + x) : 1.0 / (1.0 + x);
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    inline double tick(double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y;
        if (std::abs(z1) < 1e-25) z1 = 0.0;
        if (std::abs(z2) < 1e-25) z2 = 0.0;
        return y;
    }
    void reset() { z1 = z2 = 0.0; }
    void design(double fs, double fc, double Q, bool hp)
    {
        fc = std::min(std::max(fc, 10.0), 0.45 * fs);
        const double w0 = 2.0 * kPi * fc / fs, cs = std::cos(w0), al = std::sin(w0) / (2.0 * Q), a0 = 1.0 + al;
        if (hp) { b0 = (1 + cs) / 2 / a0; b1 = -(1 + cs) / a0; b2 = b0; }
        else    { b0 = (1 - cs) / 2 / a0; b1 = (1 - cs) / a0;  b2 = b0; }
        a1 = -2.0 * cs / a0; a2 = (1.0 - al) / a0;
    }
};

// 2x oversampler: 65-tap Kaiser half-band, linear phase, 32 samples latency (up + down) at the base rate. Streaming, no allocation.
class Oversampler2x
{
public:
    static const double* oddTaps()      // h[2j+1], j = 0..31 (the other taps of a half-band are zero, the centre is 0.5)
    {
        static double t[32]; static bool done = false;
        if (! done)
        {
            auto i0 = [](double x) { double s = 1, t2 = 1; for (int k = 1; k < 40; ++k) { t2 *= (x / (2 * k)) * (x / (2 * k)); s += t2; } return s; };
            const double beta = 7.0; double sum = 0;
            for (int j = 0; j < 32; ++j)
            {
                const int k = 2 * j + 1, m = k - 32; const double r = (double) k / 32.0 - 1.0;
                const double w = i0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0(beta);
                t[j] = std::sin(kPi * m / 2.0) / (kPi * m) * w; sum += t[j];
            }
            for (int j = 0; j < 32; ++j) t[j] *= 0.5 / sum;   // the odd taps add up to exactly 0.5: unity gain at DC
            done = true;
        }
        return t;
    }
    void reset() { std::memset(this, 0, sizeof *this); }
    inline void up(double x, double& y0, double& y1)
    {
        xp = (xp + 1) & 63; xh[xp] = x;
        const double* h = oddTaps(); double s = 0;
        for (int j = 0; j < 32; ++j) s += h[j] * xh[(xp - j) & 63];
        y0 = xh[(xp - 16) & 63]; y1 = 2.0 * s;
    }
    inline double down(double w0, double w1)
    {
        ep = (ep + 1) & 63; eh[ep] = w0;
        const double* h = oddTaps(); double s = 0.5 * eh[(ep - 16) & 63];
        for (int j = 0; j < 32; ++j) s += h[j] * oh[(op - j) & 63];
        op = (op + 1) & 63; oh[op] = w1;
        return s;
    }
private:
    double xh[64], eh[64], oh[64]; int xp, ep, op;
};

inline double softClipK(double x, double knee, double ceil)
{
    const double a = std::abs(x); if (a <= knee) return x;
    return std::copysign(knee + (ceil - knee) * std::tanh((a - knee) / (ceil - knee)), x);
}

inline double dbToLin(double db) { return std::pow(10.0, db * 0.05); }
// LA-2A style opto leveller (two-stage release with memory), same behaviour as the Meowximizer one. amount 0..1.
struct Opto
{
    double gr1 = 0, gr2 = 0, mem = 0, env = 0;
    void reset() { gr1 = gr2 = mem = env = 0; }
    double gain(double peak, double amount, double fs)
    {
        const double p = std::min(1.0, std::max(0.0, amount)), k = 1.0 - std::exp(-1.0 / (0.0015 * fs));
        env += k * (peak - env);
        const double db = 20.0 * std::log10(std::max(1e-9, env)), thr = -6.0 - 30.0 * p, knee = 12.0, over = db - thr;
        double target = 0.0;
        if (over > knee * 0.5) target = over * (2.0 / 3.0);
        else if (over > -knee * 0.5) { const double t = over + knee * 0.5; target = (2.0 / 3.0) * t * t / (2.0 * knee); }
        const double att = 1.0 - std::exp(-1.0 / (0.010 * fs)), rel1 = 1.0 - std::exp(-1.0 / (0.0866 * fs)), tau2 = 1.0 + 4.0 * mem, rel2 = 1.0 - std::exp(-1.0 / (tau2 * fs));
        gr1 += (target > gr1 ? att : rel1) * (target - gr1); gr2 += (target > gr2 ? att : rel2) * (target - gr2);
        mem += (1.0 - std::exp(-1.0 / (2.0 * fs))) * (std::min(1.0, std::max(0.0, 0.5 * (gr1 + gr2) / 15.0)) - mem);
        return dbToLin(-0.5 * (gr1 + gr2) + 6.0 * p);
    }
};

class Engine
{
public:
    static constexpr int kMaxLin = 2049, kRing = 4096, kDryRing = 8192, kMaxLook = 576;

    void prepare(double sampleRate)
    {
        fs = std::max(8000.0, std::min(384000.0, sampleRate));
        Oversampler2x::oddTaps();   // build the half-band taps now, not on the audio thread
        look = lookaheadFor(fs);
        reset();
        initialised = false;
    }
    void reset()
    {
        for (int c = 0; c < 2; ++c)
        {
            ovD[c].reset(); ovC[c].reset(); dcD[c] = 0.0; dcAcc[c] = 0.0; fz[c] = 0.0; std::memset(loudDry[c], 0, sizeof loudDry[c]);
            for (auto* st : { lp1[c], hp1[c], lp2[c], hp2[c], lpc[c], hpc[c] }) for (int k = 0; k < 4; ++k) st[k].reset();
            std::memset(linBuf[c], 0, sizeof linBuf[c]); std::memset(dryBuf[c], 0, sizeof dryBuf[c]); std::memset(limBuf[c], 0, sizeof limBuf[c]);
        }
        linPos = 0; dryPos = 0; limPos = 0; loudPos = 0; opto.reset(); ld = 0.0; mkSm = 1.0; for (auto& v : mkS) v = 1.0;
        std::memset(tgt, 0, sizeof tgt); std::memset(mins, 0, sizeof mins); for (auto& v : tgt) v = 1.0; for (auto& v : mins) v = 1.0;
        gRel = 1.0; limMix = 1.0; phase = 1.0; sinceHit = 1e12; holdoff = 0; e2 = 0.0; active = false;
        for (int b = 0; b < 3; ++b) { gs[b] = 1.0; pk[b] = 0.0; pkPost[b] = 0.0; }
        coefKey = -1.0; linKey = -1.0; waveColCur = 0; wavePkAcc = 0.0; grNow = 0.0;
        for (auto& w : wave) w.store(0.f, std::memory_order_relaxed);
    }
    int latency(const Params& p) const { return latencyFor(fs, p.linear, p.slope); }

    // in-place stereo processing. sc may be null (then the main input is the detector).
    // tables: 3 lanes of (kTable + 1) floats each (low, mid, high), see bakeCurve().
    void process(float* l, float* r, const float* scl, const float* scr, int n, const Params& p, const Timing& t, const float* tables)
    {
        const int usedLat = latency(p);
        const bool lin = p.linear;
        double bpm = std::min(400.0, std::max(20.0, t.bpm > 0 ? t.bpm : 120.0));
        const double cycleSec = p.sync ? std::max(1e-3, p.cycleBeats) * 60.0 / bpm : 1.0 / std::min(40.0, std::max(0.01, p.hz));
        const double offSec = p.offsetSamples ? p.offset / fs : p.offset * 0.001;
        const double offPh = offSec / cycleSec;
        const double sw = std::min(1.0, std::max(0.0, p.swing * 0.01)), mid = 0.5 + 0.25 * sw;
        const double tauS = 0.0003 + std::min(1.0, std::max(0.0, p.smooth * 0.01)) * 0.02, coefS = 1.0 - std::exp(-1.0 / (tauS * fs));
        const double ceilLin = std::pow(10.0, std::min(0.0, std::max(-24.0, p.ceilDb)) / 20.0);
        const double thr = std::pow(10.0, std::min(0.0, std::max(-70.0, p.threshDb)) / 20.0);
        const double detAtt = std::exp(-1.0 / (0.25 * fs)), peakDec = std::exp(-2.302585093 / fs);
        const int nb = p.slope <= 0 ? 1 : p.slope == 1 ? 2 : 4;
        updateCrossover(p, lin);
        auto lane = [&](int b) { return tables + (p.linked || ! p.multiband ? 1 : b) * (kTable + 1); };
        const float* L0 = lane(0); const float* L1 = lane(1); const float* L2 = lane(2);
        const double depthM = std::min(1.0, std::max(0.0, p.amount * 0.01));
        const double wTarget = p.amount > 0.0 ? 1.0 : 0.0;
        if (! initialised) { wMix = wTarget; for (int b = 0; b < 3; ++b) depth[b] = 0.0; for (int k = 0; k < 6; ++k) drv[k] = driveTarget(p, k); ld = p.loud * 0.01; mkSm = 1.0; initialised = true; for (int b = 0; b < 3; ++b) depth[b] = bandAmt(p, b) * depthM; }
        const double stepW = 1.0 / (0.02 * fs), rampN = 1.0 / (0.015 * fs);
        const bool hostLock = p.sync && t.havePpq && t.playing && p.trigger == 0;
        const double ppqStep = bpm / 60.0 / fs;
        double wavePk = wavePkAcc; int waveCol = waveColCur;
        {   // level match of the smooth stages (RMS of a sine, like PurrOne): once per block, smoothed below
            static const int ty[3] = { 2, 1, 0 };
            for (int j = 0; j < 3; ++j)
            {
                const double d = driveTarget(p, j) * 0.01, g = 1.0 + 11.0 * d * d, kq = std::min(1.0, d * 4.0); double a = 0, b = 0;
                if (d > 1e-4) for (int q = 0; q < 48; ++q) { const double x = 0.3 * std::sin(2.0 * kPi * q / 48.0), o = x + kq * (driveShape(ty[j], g * x) - x); a += x * x; b += o * o; }
                mkT[j] = (d > 1e-4 && b > 1e-12) ? std::min(4.0, std::max(0.25, std::sqrt(a / b))) : 1.0;
                mkS[j] += (mkT[j] - mkS[j]) * (1.0 - std::exp(-(double) n / (0.02 * fs)));
            }
        }
        {   // level match of the PurrOne stages: once per block, then smoothed (about 20 ms)
            const float pd = (float) (driveTarget(p, 3) * 0.01), pt = (float) (driveTarget(p, 4) * 0.01), pf = (float) (driveTarget(p, 5) * 0.01);
            const double mt = (pd > 1e-4f || pt > 1e-4f || pf > 1e-4f) ? (double) catfx::purr::makeup(pd, pt, pf, 0.3f) : 1.0;
            mkSm += (mt - mkSm) * (1.0 - std::exp(-(double) n / (0.02 * fs)));
        }
        for (int i = 0; i < n; ++i)
        {
            double xl = (double) l[i], xr = (double) r[i];
            if (! std::isfinite(xl)) xl = 0.0;
            if (! std::isfinite(xr)) xr = 0.0;
            // dry delay line
            dryBuf[0][dryPos] = (float) xl; dryBuf[1][dryPos] = (float) xr;
            const int rd = (dryPos - usedLat) & (kDryRing - 1);
            const double dl = dryBuf[0][rd], dr = dryBuf[1][rd];
            dryPos = (dryPos + 1) & (kDryRing - 1);

            // detection and phase
            double det = std::max(std::abs(xl), std::abs(xr));
            if (p.scTrigger && scl != nullptr) det = std::max(std::abs((double) scl[i]), scr != nullptr ? std::abs((double) scr[i]) : 0.0);
            if (p.trigger == 1 || p.scTrigger)
            {
                if (holdoff > 0) --holdoff;
                if (det > thr && det > 1.8 * e2 && holdoff == 0) { sinceHit = 0.0; active = true; holdoff = (int) (0.05 * fs); }
                e2 += (det - e2) * (1.0 - detAtt);
                phase = active ? std::min(1.0, sinceHit / (cycleSec * fs)) : 1.0;
                sinceHit += 1.0;
            }
            else
            {
                if (hostLock) { const double q = (t.ppq + (double) i * ppqStep) / std::max(1e-3, p.cycleBeats); phase = q - std::floor(q); }
                else { phase += 1.0 / (cycleSec * fs); phase -= std::floor(phase); }
            }
            const bool hit = (p.trigger == 1 || p.scTrigger);
            double pe = phase - offPh; bool open = false;
            if (hit) { if (pe < 0.0) open = true; pe = std::min(1.0, std::max(0.0, pe)); } else pe -= std::floor(pe);
            const double pw = pe < mid ? pe / mid * 0.5 : 0.5 + (pe - mid) / (1.0 - mid) * 0.5;
            const double pos = std::min(1.0, std::max(0.0, pw)) * kTable; const int ix = std::min(kTable - 1, (int) pos); const double fr = pos - ix;
            double v[3];
            v[0] = open ? 1.0 : L0[ix] + (L0[ix + 1] - L0[ix]) * fr;
            v[1] = open ? 1.0 : L1[ix] + (L1[ix + 1] - L1[ix]) * fr;
            v[2] = open ? 1.0 : L2[ix] + (L2[ix + 1] - L2[ix]) * fr;
            // display waveform folded over the cycle
            { const double ph = phase; const int col = std::min(255, std::max(0, (int) (ph * 256.0)));
              if (col != waveCol) { wave[waveCol].store((float) wavePk, std::memory_order_relaxed); wavePk = 0.0; waveCol = col; }
              wavePk = std::max(wavePk, std::max(std::abs(xl), std::abs(xr))); }

            // parameter ramps
            for (int b = 0; b < 3; ++b) { const double tg = bandAmt(p, b) * depthM; depth[b] += std::max(-rampN, std::min(rampN, tg - depth[b])); }
            for (int k = 0; k < 6; ++k) drv[k] += std::max(-rampN * 100.0, std::min(rampN * 100.0, driveTarget(p, k) - drv[k]));
            if (wMix < wTarget) wMix = std::min(wTarget, wMix + stepW); else if (wMix > wTarget) wMix = std::max(wTarget, wMix - stepW);

            // drive (2x oversampled): Soft, Tape, Tube (smooth) then PurrOne Drive, Transistor, Fuzz, in series. Level stays about the same.
            double yl = xl, yr = xr;
            {
                double a0, a1, b0, b1;
                ovD[0].up(xl, a0, a1); ovD[1].up(xr, b0, b1);
                double gk[3], kk[3], mk[3];
                const int types[3] = { 2, 1, 0 };
                for (int j = 0; j < 3; ++j)
                {
                    const double d = drv[j] * 0.01; gk[j] = 1.0 + 11.0 * d * d; kk[j] = std::min(1.0, d * 4.0);
                    mk[j] = mkS[j];
                }
                const float pd = (float) (drv[3] * 0.01), pt = (float) (drv[4] * 0.01), pf = (float) (drv[5] * 0.01);
                const bool purr = pd > 1e-4f || pt > 1e-4f || pf > 1e-4f;
                const double mkP = purr ? mkSm : 1.0;
                const double fzA = 1.0 - std::exp(-2.0 * kPi * 11000.0 / (2.0 * fs)), fzMix = std::min(1.0, 0.8 * (double) pf);
                auto sh = [&](double x, int c)
                {
                    double o = x;
                    for (int j = 0; j < 3; ++j) if (drv[j] > 0.01) { const double s2 = mk[j] * driveShape(types[j], gk[j] * o); o = o + kk[j] * (s2 - o); }
                    if (purr)
                    {
                        o = mkP * (double) catfx::purr::shaper((float) o, pd, pt, pf);
                        if (pf > 1e-4f) { fz[c] += fzA * (o - fz[c]); o += fzMix * (fz[c] - o); }
                    }
                    dcD[c] += 0.0004 * ((o - x) - dcD[c]); return o - dcD[c];
                };
                const double s0l = sh(a0, 0), s0r = sh(b0, 1), s1l = sh(a1, 0), s1r = sh(b1, 1);
                yl = ovD[0].down(s0l, s1l); yr = ovD[1].down(s0r, s1r);
            }

            // split and shape
            double band[3][2]; double outL = 0.0, outR = 0.0;
            if (! p.multiband)
            {
                band[0][0] = band[0][1] = band[2][0] = band[2][1] = 0.0;
                if (lin) linDelay(yl, yr, band); else { band[1][0] = yl; band[1][1] = yr; }
            }
            else if (lin) linSplit(yl, yr, band);
            else iirSplit(yl, yr, nb, band);
            const double gTarget[3] = { 1.0 - depth[0] * (1.0 - v[0]), 1.0 - depth[1] * (1.0 - v[1]), 1.0 - depth[2] * (1.0 - v[2]) };
            for (int b = 0; b < 3; ++b)
            {
                if (! p.multiband && b != 1) continue;
                gs[b] += (gTarget[b] - gs[b]) * coefS;
                const double pre = std::max(std::abs(band[b][0]), std::abs(band[b][1]));
                pk[b] = std::max(pre, pk[b] * peakDec);
                const bool take = p.solo == 0 || p.solo == b + 1 || ! p.multiband;
                const double bl = band[b][0] * gs[b], br = band[b][1] * gs[b];
                pkPost[b] = std::max(std::max(std::abs(bl), std::abs(br)), pkPost[b] * peakDec);
                if (take) { outL += bl; outR += br; }
            }
            if (! std::isfinite(outL) || ! std::isfinite(outR)) { reset(); outL = outR = 0.0; }

            // Loudness: LA-2A style opto + hidden soft clipper at the limiter ceiling, dry/wet. 0 % is the delayed signal, bit for bit.
            double ll = outL, lr = outR;
            {
                const double lt = std::min(1.0, std::max(0.0, p.loud * 0.01)); ld += std::max(-stepW, std::min(stepW, lt - ld));
                loudDry[0][loudPos & 63] = outL; loudDry[1][loudPos & 63] = outR;
                const double dryL = loudDry[0][(loudPos - kOsLatency) & 63], dryR = loudDry[1][(loudPos - kOsLatency) & 63];
                ++loudPos;
                const double go = opto.gain(std::max(std::abs(outL), std::abs(outR)), 0.5 * ld, fs) * dbToLin(12.0 * ld);
                double c0, c1, d0, d1;
                ovC[0].up(outL * go, c0, c1); ovC[1].up(outR * go, d0, d1);
                const double kn = 0.72 * ceilLin;
                const double wl = ovC[0].down(softClipK(c0, kn, ceilLin), softClipK(c1, kn, ceilLin));
                const double wr = ovC[1].down(softClipK(d0, kn, ceilLin), softClipK(d1, kn, ceilLin));
                const double m = std::min(1.0, ld * 1.5);
                ll = dryL * (1.0 - m) + wl * m; lr = dryR * (1.0 - m) + wr * m;
            }
            // lookahead limiter (always delays by `look` so the latency never changes)
            {
                limBuf[0][limPos] = ll; limBuf[1][limPos] = lr;
                const double peak = std::max(std::abs(ll), std::abs(lr));
                const double target = peak > ceilLin ? ceilLin / peak : 1.0;
                tgt[limPos] = target;
                double mn = 1.0; const int W = look + 1;
                for (int k = 0; k < W; ++k) mn = std::min(mn, tgt[(limPos - k) & (kLim - 1)]);
                mins[limPos] = mn;
                double avg = 0.0; for (int k = 0; k < look; ++k) avg += mins[(limPos - k) & (kLim - 1)];
                avg /= look;
                gRel = std::min(avg, gRel + (1.0 - gRel) * (1.0 - std::exp(-1.0 / (0.08 * fs))));
                const double tm = p.limOn ? 1.0 : 0.0; limMix += std::max(-0.002, std::min(0.002, tm - limMix));
                const double gain = 1.0 + (gRel - 1.0) * limMix;
                const int rl = (limPos - look) & (kLim - 1);
                ll = limBuf[0][rl] * gain; lr = limBuf[1][rl] * gain;
                limPos = (limPos + 1) & (kLim - 1);
                grNow = std::max(grNow * 0.9995, -20.0 * std::log10(std::max(1e-4, gain)));
            }
            l[i] = (float) (dl * (1.0 - wMix) + ll * wMix);
            r[i] = (float) (dr * (1.0 - wMix) + lr * wMix);
        }
        wavePkAcc = wavePk; waveColCur = waveCol;
        mPhase.store((float) phase, std::memory_order_relaxed);
        for (int b = 0; b < 3; ++b) { mGain[b].store((float) gs[b], std::memory_order_relaxed); mPre[b].store((float) pk[b], std::memory_order_relaxed); mPost[b].store((float) pkPost[b], std::memory_order_relaxed); }
        mGr.store((float) grNow, std::memory_order_relaxed);
    }

    // read by the editor (any thread)
    std::atomic<float> mPhase { 0.f }, mGr { 0.f }, mGain[3] { { 1.f }, { 1.f }, { 1.f } }, mPre[3] { { 0.f }, { 0.f }, { 0.f } }, mPost[3] { { 0.f }, { 0.f }, { 0.f } }, wave[256];

private:
    static constexpr int kLim = 1024;
    double fs = 44100.0; int look = 72; bool initialised = false;
    Biquad lp1[2][4], hp1[2][4], lp2[2][4], hp2[2][4], lpc[2][4], hpc[2][4];
    double coefKey = -1.0; int nbUsed = 1; double polarity = 1.0;
    Oversampler2x ovD[2], ovC[2];
    double dcD[2], dcAcc[2], fz[2];
    double mkT[3] { 1, 1, 1 }, mkS[3] { 1, 1, 1 }; double drv[6] { 0, 0, 0, 0, 0, 0 }, ld = 0.0, mkSm = 1.0; Opto opto; double loudDry[2][64]; int loudPos = 0;
    double scratch[kMaxLin];
    float linBuf[2][2 * kRing]; int linPos = 0; float k1[kMaxLin], k2[kMaxLin]; int linN = 0; double linKey = -1.0;
    float dryBuf[2][kDryRing]; int dryPos = 0;
    double limBuf[2][kLim]; int limPos = 0; double tgt[kLim], mins[kLim]; double gRel = 1.0, limMix = 1.0, grNow = 0.0;
    double phase = 1.0, sinceHit = 1e12, e2 = 0.0; int holdoff = 0; bool active = false;
    double gs[3] { 1, 1, 1 }, pk[3] { 0, 0, 0 }, pkPost[3] { 0, 0, 0 }, depth[3] { 0, 0, 0 }, wMix = 0.0;
    double wavePkAcc = 0.0; int waveColCur = 0;

    static double bandAmt(const Params& p, int b) { return p.multiband ? (b == 0 ? p.amtLo : b == 1 ? p.amtMid : p.amtHi) * 0.01 : 1.0; }

    static double driveTarget(const Params& p, int k)
    {
        const double v[6] = { p.dSoft, p.dTape, p.dTube, p.dDrive, p.dTrans, p.dFuzz };
        return std::min(100.0, std::max(0.0, v[k]));
    }
    static double driveShape(int type, double u)
    {
        switch (type)
        {
            case 1: return u / std::pow(1.0 + std::pow(std::abs(u), 2.2), 1.0 / 2.2);
            case 2: return std::atan(1.5707963 * u) * 0.6366198;
            default: return std::tanh(u + 0.18) - std::tanh(0.18);
        }
    }

    void updateCrossover(const Params& p, bool lin)
    {
        if (lin)
        {
            const double key = p.xLo * 1000.0 + p.xHi * 7.0 + p.slope * 1e9;
            if (key == linKey) return;
            linKey = key;
            linN = linTaps(p.slope);
            designLp(k1, linN, p.xLo); designLp(k2, linN, p.xHi);
            return;
        }
        const double key = p.xLo * 1000.0 + p.xHi * 7.0 + p.slope * 1e9 + 0.5;
        if (key == coefKey) return;
        coefKey = key;
        static const double q2[1] = { 0.5 }, q4[2] = { 0.70710678, 0.70710678 }, q8[4] = { 0.54119610, 1.30656296, 0.54119610, 1.30656296 };
        const double* qs = p.slope <= 0 ? q2 : p.slope == 1 ? q4 : q8; nbUsed = p.slope <= 0 ? 1 : p.slope == 1 ? 2 : 4; polarity = p.slope <= 0 ? -1.0 : 1.0;
        const double f1 = std::min(p.xLo, p.xHi * 0.9), f2 = std::max(p.xHi, f1 * 1.1);
        for (int c = 0; c < 2; ++c)
            for (int k = 0; k < nbUsed; ++k)
            { lp1[c][k].design(fs, f1, qs[k], false); hp1[c][k].design(fs, f1, qs[k], true); lp2[c][k].design(fs, f2, qs[k], false); hp2[c][k].design(fs, f2, qs[k], true);
              lpc[c][k].design(fs, f2, qs[k], false); hpc[c][k].design(fs, f2, qs[k], true); }
    }
    void designLp(float* k, int N, double fc)
    {
        const double wc = 2.0 * kPi * std::min(fc, 0.45 * fs) / fs, c = (N - 1) * 0.5; double sum = 0.0;
        double* t = scratch;   // preallocated: nothing is allocated while processing
        for (int i = 0; i < N; ++i)
        {
            const double m = i - c, w = 0.42 - 0.5 * std::cos(2 * kPi * i / (N - 1)) + 0.08 * std::cos(4 * kPi * i / (N - 1));
            t[i] = (m == 0.0 ? wc / kPi : std::sin(wc * m) / (kPi * m)) * w; sum += t[i];
        }
        for (int i = 0; i < N; ++i) k[i] = (float) (t[i] / sum);
    }
    static double cascade(Biquad* s, int n, double x) { for (int k = 0; k < n; ++k) x = s[k].tick(x); return x; }
    void iirSplit(double yl, double yr, int nb, double band[3][2])
    {
        const double in[2] = { yl, yr };
        for (int c = 0; c < 2; ++c)
        {
            const double lo = cascade(lp1[c], nb, in[c]), rest = polarity * cascade(hp1[c], nb, in[c]);
            const double midv = cascade(lp2[c], nb, rest), hi = polarity * cascade(hp2[c], nb, rest);
            const double loC = cascade(lpc[c], nb, lo) + polarity * cascade(hpc[c], nb, lo);   // all-pass at the upper crossover keeps the phase aligned
            band[0][c] = loC; band[1][c] = midv; band[2][c] = hi;
        }
    }
    static float dotk(const float* a, const float* b, int n)
    {
        float s0 = 0, s1 = 0, s2 = 0, s3 = 0; int i = 0;
        for (; i + 3 < n; i += 4) { s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3]; }
        for (; i < n; ++i) s0 += a[i] * b[i];
        return (s0 + s1) + (s2 + s3);
    }
    void pushLin(double yl, double yr) { linPos = (linPos + 1) & (kRing - 1); linBuf[0][linPos] = linBuf[0][linPos + kRing] = (float) yl; linBuf[1][linPos] = linBuf[1][linPos + kRing] = (float) yr; }
    void linSplit(double yl, double yr, double band[3][2])
    {
        pushLin(yl, yr);
        const int cdel = (linN - 1) / 2;
        for (int c = 0; c < 2; ++c)
        {
            const float* w = linBuf[c] + linPos + kRing - (linN - 1);   // oldest .. newest (the kernels are symmetric)
            const double lo = dotk(k1, w, linN), l2 = dotk(k2, w, linN), x = linBuf[c][linPos + kRing - cdel];
            band[0][c] = lo; band[1][c] = l2 - lo; band[2][c] = x - l2;
        }
    }
    void linDelay(double yl, double yr, double band[3][2])
    {
        // single band with linear phase on: a pure delay equal to the split's, so the reported latency stays the same
        pushLin(yl, yr);
        const int cdel = (linN - 1) / 2;
        for (int c = 0; c < 2; ++c) band[1][c] = linBuf[c][linPos + kRing - cdel];
    }
};
}
