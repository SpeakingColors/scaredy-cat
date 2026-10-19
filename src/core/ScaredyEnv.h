// Scaredy Cat: volume-shaper envelope model (pure C++, no JUCE). Original code, AGPL-3.0-or-later.
// A curve is a list of points (x 0..1 over one cycle, y 0..1 gain, c = curvature of the segment that starts at this point).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace scaredy
{
constexpr int kTable = 1024;          // table resolution over one cycle (the table holds kTable + 1 values)
constexpr int kMaxPts = 128;
constexpr float kMinGap = 0.002f;     // closest two points may be (fraction of a cycle)

struct Pt { float x, y, c; };
using Curve = std::vector<Pt>;

inline float segShape(float t, float c) { return std::pow(std::min(1.f, std::max(0.f, t)), std::exp2(2.f * c)); }

inline void sanitize(Curve& cv)
{
    for (auto& p : cv)
    {
        if (! std::isfinite(p.x)) p.x = 0.f;
        if (! std::isfinite(p.y)) p.y = 1.f;
        if (! std::isfinite(p.c)) p.c = 0.f;
        p.x = std::min(1.f, std::max(0.f, p.x)); p.y = std::min(1.f, std::max(0.f, p.y)); p.c = std::min(1.f, std::max(-1.f, p.c));
    }
    std::stable_sort(cv.begin(), cv.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; });
    if (cv.size() > (size_t) kMaxPts) cv.resize((size_t) kMaxPts);
    for (size_t i = 1; i < cv.size(); ++i) if (cv[i].x < cv[i - 1].x + kMinGap) cv[i].x = std::min(1.f, cv[i - 1].x + kMinGap);
    if (cv.empty()) cv = { { 0.f, 1.f, 0.f }, { 1.f, 1.f, 0.f } };
}

inline float evalCurve(const Curve& cv, float x)
{
    if (cv.empty()) return 1.f;
    if (x <= cv.front().x) return cv.front().y;
    if (x >= cv.back().x) return cv.back().y;
    size_t i = 0; while (i + 1 < cv.size() && cv[i + 1].x <= x) ++i;
    const Pt& a = cv[i]; const Pt& b = cv[i + 1];
    const float t = (x - a.x) / std::max(1e-6f, b.x - a.x);
    return a.y + (b.y - a.y) * segShape(t, a.c);
}

// table[0..kTable]: the curve sampled over one cycle (last entry is phase 1.0)
inline void bakeCurve(const Curve& cv, float* table)
{
    for (int i = 0; i <= kTable; ++i) table[i] = evalCurve(cv, (float) i / (float) kTable);
}

inline std::string curveToString(const Curve& cv)
{
    std::string s; char b[64];
    for (auto& p : cv) { std::snprintf(b, sizeof b, "%.4f:%.4f:%.3f;", (double) p.x, (double) p.y, (double) p.c); s += b; }
    return s;
}

inline Curve curveFromString(const std::string& s)
{
    Curve cv; const char* q = s.c_str();
    while (*q != 0 && cv.size() < (size_t) kMaxPts)
    {
        char* e = nullptr; Pt p { 0.f, 1.f, 0.f };
        p.x = (float) std::strtod(q, &e); if (e == q || *e != ':') break; q = e + 1;
        p.y = (float) std::strtod(q, &e); if (e == q || *e != ':') break; q = e + 1;
        p.c = (float) std::strtod(q, &e); if (e == q) break; q = e;
        cv.push_back(p);
        while (*q != 0 && *q != ';') ++q;
        if (*q == ';') ++q;
    }
    sanitize(cv); return cv;
}

// Add a point on the curve at x (keeps the shape); returns its index.
inline int insertPoint(Curve& cv, float x)
{
    const float y = evalCurve(cv, x);
    size_t i = 0; while (i < cv.size() && cv[i].x < x) ++i;
    const float c = (i > 0 ? cv[i - 1].c : 0.f);
    cv.insert(cv.begin() + (long) i, Pt { x, y, c });
    sanitize(cv);
    for (size_t k = 0; k < cv.size(); ++k) if (std::abs(cv[k].x - x) < 0.01f) return (int) k;
    return (int) i;
}

// Shape presets (the glyph row under the editor). lo = lowest gain (0 = fully ducked).
constexpr int kNumShapes = 12;
inline const char* shapeName(int id)
{
    static const char* n[kNumShapes] = { "Pump", "Gate", "Swell", "Ramp up", "Step", "Wave", "Ramp down", "V", "Trance gate x4", "Offbeat pump", "Random steps", "Tremolo" };
    return n[std::max(0, std::min(kNumShapes - 1, id))];
}
inline Curve shapeCurve(int id, float lo = 0.f, unsigned seed = 1)
{
    Curve c;
    switch (id)
    {
        case 0: c = { { 0, lo, 0.5f }, { 0.6f, 1, 0 }, { 1, 1, 0 } }; break;
        case 1: c = { { 0, lo, 0 }, { 0.5f, lo, 0 }, { 0.504f, 1, 0 }, { 1, 1, 0 } }; break;
        case 2: c = { { 0, lo, -0.6f }, { 1, 1, 0 } }; break;
        case 3: c = { { 0, lo, 0 }, { 1, 1, 0 } }; break;
        case 4: c = { { 0, lo, 0 }, { 0.25f, lo, 0 }, { 0.254f, 1, 0 }, { 1, 1, 0 } }; break;
        case 5: c = { { 0, 1, 0.35f }, { 0.5f, lo, -0.35f }, { 1, 1, 0 } }; break;
        case 6: c = { { 0, 1, 0 }, { 1, lo, 0 } }; break;
        case 7: c = { { 0, 1, 0 }, { 0.5f, lo, 0 }, { 1, 1, 0 } }; break;
        case 8: for (int i = 0; i < 4; ++i) { const float q = (float) i * 0.25f; c.push_back({ q, lo, 0 }); c.push_back({ q + 0.1f, lo, 0 }); c.push_back({ q + 0.104f, 1, 0 }); c.push_back({ q + 0.23f, 1, 0 }); } break;
        case 9: c = { { 0, lo, 0.5f }, { 0.3f, 1, 0 }, { 0.48f, 1, 0 }, { 0.5f, lo, 0.5f }, { 0.8f, 1, 0 }, { 1, 1, 0 } }; break;
        case 10: { unsigned s = seed * 2654435761u + 12345u; for (int i = 0; i < 8; ++i) { s = s * 1664525u + 1013904223u; const float v = ((s >> 8) & 0xffff) / 65535.f; const float x0 = (float) i / 8.f; const float y = lo + (1.f - lo) * v; c.push_back({ x0, y, 0 }); c.push_back({ x0 + 0.1f, y, 0 }); } break; }
        default: for (int i = 0; i <= 32; ++i) { const float x = (float) i / 32.f; c.push_back({ x, lo + (1.f - lo) * (0.5f + 0.5f * std::cos(8.f * 3.14159265f * x)), 0 }); } break;
    }
    sanitize(c); return c;
}

// Operations used by the editor
inline void reverseCurve(Curve& cv) { Curve r; for (size_t i = 0; i < cv.size(); ++i) { Pt p = cv[cv.size() - 1 - i]; p.x = 1.f - p.x; p.c = -p.c; r.push_back(p); } cv = r; sanitize(cv); }
inline void flipCurve(Curve& cv) { for (auto& p : cv) { p.y = 1.f - p.y; p.c = -p.c; } sanitize(cv); }
// Move every point by dx (fraction of a cycle), wrapping around; the curve keeps its shape and the ends stay joined.
inline void shiftCurve(Curve& cv, float dx)
{
    if (cv.empty()) return;
    const Curve old = cv;
    for (auto& p : cv) { p.x += dx; p.x -= std::floor(p.x); }
    std::stable_sort(cv.begin(), cv.end(), [](const Pt& a, const Pt& b) { return a.x < b.x; });
    auto at = [&](float x) { float u = x - dx; u -= std::floor(u); return evalCurve(old, u); };
    if (cv.front().x > kMinGap) cv.insert(cv.begin(), Pt { 0.f, at(0.f), 0.f });
    if (cv.back().x < 1.f - kMinGap) cv.push_back(Pt { 1.f, at(1.f - 1e-4f), 0.f });
    sanitize(cv);
}
}
