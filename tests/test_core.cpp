// Scaredy Cat core tests (no JUCE): null at 0%, level, pump depth, no NaN, latency, linear-phase split.
#include "../src/core/ScaredyCore.h"
#include <cstdio>
#include <random>
using namespace scaredy;

static int fails = 0;
#define CHECK(c, ...) do { if (! (c)) { ++fails; std::printf("FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void tablesFor(std::vector<float>& tb, int shape, float lo)
{
    tb.assign(3 * (size_t) (kTable + 1), 1.f);
    for (int b = 0; b < 3; ++b) bakeCurve(shapeCurve(shape, lo), tb.data() + b * (kTable + 1));
}
static void flat(std::vector<float>& tb) { tb.assign(3 * (size_t) (kTable + 1), 1.f); }
static double goertzel(const std::vector<float>& x, size_t a, size_t n, double f, double fs)
{
    const double w = 2 * kPi * f / fs, c = 2 * std::cos(w); double s1 = 0, s2 = 0;
    for (size_t i = a; i < a + n; ++i) { const double s = x[i] + c * s1 - s2; s2 = s1; s1 = s; }
    return std::sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2.0 / (double) n;
}
static void run(Engine& e, std::vector<float>& l, std::vector<float>& r, const Params& p, const Timing& t, const std::vector<float>& tb)
{
    const size_t N = l.size(); const int B = 480;   // odd-sized blocks on purpose
    Timing tt = t;
    for (size_t i = 0; i < N; i += B) { const int n = (int) std::min<size_t>(B, N - i); e.process(l.data() + i, r.data() + i, nullptr, nullptr, n, p, tt, tb.data()); tt.ppq += (double) n * tt.bpm / 60.0 / 48000.0; }
}

int main()
{
    const double fs = 48000.0; std::vector<float> tb; Timing tm; tm.playing = true; tm.havePpq = true; tm.bpm = 120.0;
    std::mt19937 rng(7); std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    const size_t N = (size_t) (fs * 3);

    // 1. null at 0%: output is the input delayed by the reported latency, bit for bit
    for (int mode = 0; mode < 2; ++mode)
    {
        Engine e; e.prepare(fs); Params p; p.amount = 0.0; p.linear = mode == 1; p.dTube = 60.0; p.dFuzz = 40.0; p.loud = 0.0; tablesFor(tb, 0, 0.f);
        std::vector<float> in(N), l, r; for (auto& v : in) v = u(rng); l = in; r = in; run(e, l, r, p, tm, tb);
        const int lat = e.latency(p); size_t bad = 0;
        for (size_t i = (size_t) lat; i < N; ++i) if (l[i] != in[i - (size_t) lat] || r[i] != in[i - (size_t) lat]) ++bad;
        CHECK(bad == 0, "null test (%s): %zu samples differ", mode ? "linear" : "min phase", bad);
        std::printf("null at 0%% (%s), latency %d samples: %s\n", mode ? "linear" : "min phase", lat, bad == 0 ? "exact" : "FAILED");
    }

    // 2. flat envelopes at 100%: the tones keep their level (all-pass crossovers, transparent drive off)
    for (int mode = 0; mode < 2; ++mode)
    {
        Engine e; e.prepare(fs); Params p; p.amount = 100.0; p.linear = mode == 1; flat(tb);
        std::vector<float> l(N), r; const double fr[4] = { 60, 700, 5000, 12000 };
        for (size_t i = 0; i < N; ++i) { double s = 0; for (double f : fr) s += 0.1 * std::sin(2 * kPi * f * (double) i / fs); l[i] = (float) s; }
        r = l; run(e, l, r, p, tm, tb);
        for (double f : fr) { std::vector<float> ref(N); for (size_t i = 0; i < N; ++i) ref[i] = (float) std::sin(2 * kPi * f * (double) i / fs);
            const double a = goertzel(l, N / 2, 24000, f, fs) / 0.1, dB = 20 * std::log10(a);
            CHECK(std::abs(dB) < 0.25, "flat level %s %g Hz: %.3f dB", mode ? "linear" : "min phase", f, dB); }
        std::printf("flat envelope level (%s): ok\n", mode ? "linear" : "min phase");
    }

    // 3. full pump: 1 kHz tone, 1/4 note at 120 bpm, 100%: gain ~0 at the hit, 1 before the next one
    {
        Engine e; e.prepare(fs); Params p; p.amount = 100.0; p.smooth = 0.0; tablesFor(tb, 0, 0.f);
        std::vector<float> l(N), r; for (size_t i = 0; i < N; ++i) l[i] = 0.5f * (float) std::sin(2 * kPi * 1000.0 * (double) i / fs);
        r = l; tm.ppq = 0.0; run(e, l, r, p, tm, tb);
        const int lat = e.latency(p), cyc = (int) (fs * 0.5);
        auto rms = [&](size_t a, size_t n) { double s = 0; for (size_t i = a; i < a + n; ++i) s += (double) l[i] * l[i]; return std::sqrt(s / (double) n); };
        const size_t base = (size_t) lat + (size_t) cyc * 2;
        const double deep = rms(base + 48, 200), open = rms(base + (size_t) (cyc * 0.9), 400);
        std::printf("pump: ducked %.1f dB, open %.1f dB\n", 20 * std::log10(deep / 0.3535), 20 * std::log10(open / 0.3535));
        CHECK(20 * std::log10(deep / 0.3535) < -40.0, "100%% pump is not deep enough");
        CHECK(std::abs(20 * std::log10(open / 0.3535)) < 0.6, "pump does not come back to full level");
        // 50% amount is clearly in between
        Engine e2; e2.prepare(fs); p.amount = 50.0; std::vector<float> m(N), mr; for (size_t i = 0; i < N; ++i) m[i] = 0.5f * (float) std::sin(2 * kPi * 1000.0 * (double) i / fs);
        mr = m; run(e2, m, mr, p, tm, tb); double s = 0; for (size_t i = base + 48; i < base + 248; ++i) s += (double) m[i] * m[i];
        const double d50 = 20 * std::log10(std::sqrt(s / 200.0) / 0.3535); std::printf("50%%: ducked %.1f dB\n", d50);
        CHECK(d50 < -3.0 && d50 > -12.0, "50%% depth out of range: %.1f", d50);
    }

    // 4. extreme settings with loud noise: no NaN or Inf, never above the clipper ceiling
    {
        std::uniform_real_distribution<float> big(-4.f, 4.f); size_t nan = 0; double mx = 0;
        for (int trial = 0; trial < 12; ++trial)
        {
            Engine e; e.prepare(trial % 2 ? 44100.0 : 96000.0); Params p; p.amount = trial % 3 == 0 ? 100 : 37; p.dTube = trial % 3 == 0 ? 100 : 0; p.dTape = trial % 3 == 1 ? 100 : 0; p.dSoft = trial % 3 == 2 ? 100 : 0; p.dDrive = 100; p.dTrans = 100; p.dFuzz = trial % 2 ? 100 : 0; p.loud = trial % 2 ? 100 : 40; p.slope = trial % 3; p.linear = trial % 4 == 3;
            p.xLo = 30 + 40 * trial; p.xHi = 1000 + 300 * trial; p.smooth = trial * 8; p.swing = trial * 8; p.trigger = trial % 2; p.threshDb = -40; p.offset = trial * 3; p.offsetSamples = trial % 2; p.sync = trial % 2 == 0; p.hz = 5; p.cycleBeats = 0.25;
            tablesFor(tb, trial % kNumShapes, 0.f);
            std::vector<float> l(48000), r; for (auto& v : l) v = big(rng); r = l; run(e, l, r, p, tm, tb);
            for (size_t i = 0; i < l.size(); ++i) { if (! std::isfinite(l[i]) || ! std::isfinite(r[i])) ++nan; mx = std::max(mx, (double) std::abs(l[i])); }
        }
        CHECK(nan == 0, "%zu non finite samples", nan); CHECK(mx <= 0.9701, "peak %.4f above the ceiling", mx);
        std::printf("extreme settings: %zu NaN, peak %.4f\n", nan, mx);
    }

    // 5. latency is exact in linear-phase mode: an impulse comes out at the reported latency
    for (int slope = 0; slope < 3; ++slope)
    {
        Engine e; e.prepare(fs); Params p; p.amount = 100; p.linear = true; p.slope = slope; flat(tb);
        std::vector<float> l(16384, 0.f), r; l[100] = 0.8f; r = l; run(e, l, r, p, tm, tb);
        size_t pk = 0; for (size_t i = 0; i < l.size(); ++i) if (std::abs(l[i]) > std::abs(l[pk])) pk = i;
        const int lat = e.latency(p);
        CHECK((int) pk == 100 + lat, "impulse at %zu, expected %d", pk, 100 + lat);
        std::printf("linear phase slope %d: latency %d, impulse peak at +%d\n", slope, lat, (int) pk - 100);
    }

    // 6. hit mode: a click restarts the envelope
    {
        Engine e; e.prepare(fs); Params p; p.amount = 100; p.trigger = 1; p.sync = false; p.hz = 2.0; p.threshDb = -30; p.smooth = 0; tablesFor(tb, 0, 0.f);
        std::vector<float> l(N, 0.f), r; for (size_t i = 0; i < N; ++i) l[i] = 0.3f * (float) std::sin(2 * kPi * 200.0 * (double) i / fs) * (i > 4800 ? 1.f : 0.f);
        r = l; run(e, l, r, p, tm, tb); const int lat = e.latency(p);
        double s = 0; for (size_t i = 4800 + (size_t) lat + 40; i < 4800 + (size_t) lat + 240; ++i) s += (double) l[i] * l[i];
        const double d = 20 * std::log10(std::sqrt(s / 200.0) / 0.212 + 1e-9); std::printf("hit mode: ducked %.1f dB after the hit\n", d);
        CHECK(d < -20.0, "hit mode did not duck (%.1f dB)", d);
    }
    // 7. curve text round trip
    { const Curve c = shapeCurve(8, 0.2f); const Curve d = curveFromString(curveToString(c)); CHECK(c.size() == d.size(), "curve round trip size"); }
    // 8. round 3: shape 0 joins with no jump, drive knobs keep the level, Loudness is exact at 0 and louder at 100, every new knob changes the sound
    {
        const Curve c0 = shapeCurve(0, 0.f); CHECK(wrapJump(c0) < 0.01f, "default shape does not join: %.3f", (double) wrapJump(c0));
        Curve old = { { 0, 0.f, 0.5f }, { 0.6f, 1, 0 }, { 1, 1, 0 } }; CHECK(wrapJump(old) > 0.9f, "sanity: the old shape jumps");
        Curve fixd = old; wrapFix(fixd); CHECK(wrapJump(fixd) < 0.01f, "wrapFix did not join");
        auto maxStep = [&](const Curve& cv)
        {
            std::vector<float> t3(3 * (size_t) (kTable + 1)); for (int b = 0; b < 3; ++b) bakeCurve(cv, t3.data() + b * (kTable + 1));
            Engine e; e.prepare(fs); Params p; p.amount = 100; p.multiband = false; p.smooth = 4; p.limOn = false;
            std::vector<float> l((size_t) (fs * 2.2), 0.5f), r; r = l; run(e, l, r, p, tm, t3);
            float m = 0; for (size_t i = (size_t) fs / 2; i + 1 < l.size(); ++i) m = std::max(m, std::abs(l[i + 1] - l[i])); return m;
        };
        const float sNew = maxStep(c0), sOld = maxStep(old);
        std::printf("loop click: biggest step %.4f (new default) vs %.4f (old shape)\n", (double) sNew, (double) sOld);
        CHECK(sNew < 0.004f && sNew * 4.f < sOld, "default shape still clicks at the loop point");
    }
    {
        flat(tb); std::vector<float> in(N); for (size_t i = 0; i < N; ++i) in[i] = 0.25f * (float) std::sin(2 * kPi * 110.0 * (double) i / fs) + 0.12f * (float) std::sin(2 * kPi * 1770.0 * (double) i / fs) + 0.05f * u(rng);
        auto rmsOf = [&](const std::vector<float>& x) { double s = 0; for (size_t i = N / 2; i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt(s / (double) (x.size() - N / 2)); };
        const double rin = rmsOf(in);
        const char* nm[6] = { "tube", "tape", "soft", "drive", "transistor", "fuzz" };
        std::vector<float> base = in; { Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false; std::vector<float> r = base; run(e, base, r, p, tm, tb); }
        for (int k = 0; k < 6; ++k)
        {
            Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false;
            double* d[6] = { &p.dTube, &p.dTape, &p.dSoft, &p.dDrive, &p.dTrans, &p.dFuzz }; *d[k] = 50.0;
            std::vector<float> l = in, r = in; run(e, l, r, p, tm, tb);
            bool ok = true; for (float v : l) if (! std::isfinite(v)) ok = false;
            const double db = 20 * std::log10(rmsOf(l) / rin), diff = 20 * std::log10(1e-9 + [&] { double s = 0; for (size_t i = N / 2; i < l.size(); ++i) s += std::pow((double) l[i] - base[i], 2); return std::sqrt(s / (double) (N / 2)); }() / rin);
            std::printf("drive %-10s 50%%: level %+.2f dB, change vs clean %.1f dB re input\n", nm[k], db, diff);
            CHECK(ok, "NaN in drive %s", nm[k]); CHECK(std::abs(db) < 1.5, "drive %s level moved %.2f dB", nm[k], db); CHECK(diff > -30.0, "drive %s does not change the sound", nm[k]);
        }
        // Loudness: 0 % is the same as no loudness stage; 100 % is louder and never above the ceiling
        std::vector<float> a = in, b2 = in, c = in;
        { Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = true; p.loud = 0.0; std::vector<float> r = a; run(e, a, r, p, tm, tb); }
        { Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = true; p.loud = 100.0; std::vector<float> r = b2; run(e, b2, r, p, tm, tb); }
        { Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false; p.loud = 0.0; std::vector<float> r = c; run(e, c, r, p, tm, tb); }
        double dmax = 0; for (size_t i = N / 2; i < a.size(); ++i) dmax = std::max(dmax, (double) std::abs(a[i] - c[i]));
        float pkLoud = 0; for (size_t i = N / 2; i < b2.size(); ++i) pkLoud = std::max(pkLoud, std::abs(b2[i]));
        const double gl = 20 * std::log10(rmsOf(b2) / rmsOf(a));
        std::printf("loudness: 0%% vs limiter off differs by %.2g (quiet signal), 100%% is %+.1f dB, peak %.3f\n", dmax, gl, (double) pkLoud);
        CHECK(dmax < 1e-6, "loud 0 is not transparent"); CHECK(gl > 3.0, "loudness 100 is not louder (%.1f dB)", gl); CHECK(pkLoud <= 0.9662f, "loudness broke the ceiling");
        // every new knob changes the sound by itself
        for (int k = 0; k < 7; ++k)
        {
            Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false;
            double* d[7] = { &p.dTube, &p.dTape, &p.dSoft, &p.dDrive, &p.dTrans, &p.dFuzz, &p.loud }; *d[k] = 100.0;
            std::vector<float> l = in, r = in; run(e, l, r, p, tm, tb);
            double s = 0; for (size_t i = N / 2; i < l.size(); ++i) s += std::pow((double) l[i] - base[i], 2);
            CHECK(std::sqrt(s / (double) (N / 2)) > 1e-3, "knob %d does nothing", k);
        }
    }
    // closed-loop drive level staging: real material (bass-heavy, kick and hats) keeps its RMS within 0.5 dB at 50 % and 100 %
    {
        flat(tb); const size_t M = (size_t) (fs * 6);
        std::vector<float> bass(M), kh(M); std::mt19937 rg(11); std::uniform_real_distribution<float> nz(-1.f, 1.f);
        for (size_t i = 0; i < M; ++i)
        {
            const double t = (double) i / fs;
            bass[i] = (float) (0.35 * std::sin(2 * kPi * 41.0 * t) + 0.2 * std::sin(2 * kPi * 82.0 * t + 0.7) + 0.04 * std::sin(2 * kPi * 330.0 * t));
            const double bt = std::fmod(t, 0.5), ht = std::fmod(t + 0.25, 0.5);
            const double kick = 0.8 * std::exp(-bt * 14.0) * std::sin(2 * kPi * (50.0 + 90.0 * std::exp(-bt * 40.0)) * bt);
            const double hat = 0.25 * std::exp(-ht * 60.0) * (double) nz(rg);
            kh[i] = (float) (kick + hat);
        }
        const char* nm[6] = { "tube", "tape", "soft", "drive", "transistor", "fuzz" };
        auto rmsTail = [&](const std::vector<float>& x) { double a = 0; const size_t o = M / 2; for (size_t i = o; i < x.size(); ++i) a += (double) x[i] * x[i]; return std::sqrt(a / (double) (x.size() - o)); };
        for (int sig = 0; sig < 2; ++sig)
        {
            const std::vector<float>& in = sig ? kh : bass;
            std::vector<float> ref = in; { Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false; std::vector<float> r = ref; run(e, ref, r, p, tm, tb); }
            for (int k = 0; k < 6; ++k) for (int lv = 0; lv < 2; ++lv)
            {
                Engine e; e.prepare(fs); Params p; p.multiband = false; p.limOn = false;
                double* d[6] = { &p.dTube, &p.dTape, &p.dSoft, &p.dDrive, &p.dTrans, &p.dFuzz }; *d[k] = lv ? 100.0 : 50.0;
                std::vector<float> l = in, r = in; run(e, l, r, p, tm, tb);
                bool ok = true; for (float v : l) if (! std::isfinite(v)) ok = false;
                const double db = 20 * std::log10(rmsTail(l) / rmsTail(ref));
                const double dbIn = 20 * std::log10(rmsTail(l) / rmsTail(in));
                std::printf("level staging %-5s %-10s %3d %%: %+.2f dB vs drive 0, %+.2f dB vs input\n", sig ? "kick" : "bass", nm[k], lv ? 100 : 50, db, dbIn);
                CHECK(ok, "NaN in staging %s", nm[k]); CHECK(std::abs(dbIn) < 0.5, "%s %s %d %% level vs input moved %.2f dB", sig ? "kick" : "bass", nm[k], lv ? 100 : 50, dbIn); CHECK(std::abs(db) < 0.5, "%s %s %d %% level moved %.2f dB", sig ? "kick" : "bass", nm[k], lv ? 100 : 50, db);
            }
        }
        // drive 0 must be untouched by the staging: identical to a run where the drive section never woke up
        { Engine e1, e2; e1.prepare(fs); e2.prepare(fs); Params p; p.multiband = false; p.limOn = false; std::vector<float> a = bass, b = bass, r1 = bass, r2 = bass;
          run(e1, a, r1, p, tm, tb); p.dTube = 0.005; run(e2, b, r2, p, tm, tb);
          size_t bad = 0; for (size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++bad; CHECK(bad == 0, "drive below 0.01 %% is not bit exact (%zu)", bad); }
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
