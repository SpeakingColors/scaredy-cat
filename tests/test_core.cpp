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
        Engine e; e.prepare(fs); Params p; p.amount = 0.0; p.linear = mode == 1; p.drive = 60.0; tablesFor(tb, 0, 0.f);
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
            Engine e; e.prepare(trial % 2 ? 44100.0 : 96000.0); Params p; p.amount = trial % 3 == 0 ? 100 : 37; p.drive = 100; p.driveType = trial % 3; p.slope = trial % 3; p.linear = trial % 4 == 3;
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
    std::printf(fails ? "\n%d FAILED\n" : "\nall passed\n", fails);
    return fails ? 1 : 0;
}
