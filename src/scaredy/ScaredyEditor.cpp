#include "ScaredyEditor.h"
#include "common/Viz.h"
#include <array>
#include <set>
#include <catui/CatLookAndFeel.h>
#include <catui/RenderGate.h>
#include <catui/FxLogos.h>

using namespace scaredy;
using catui::Colours;

namespace
{
constexpr int kBaseW = 1180, kBaseH = 856, kMinW = 708, kMaxW = 2360, kCheek = 22, kContentW = kBaseW - 2 * kCheek;

juce::Colour bandCol(int b) { return b == 0 ? catui::SodaPop::orange() : b == 1 ? catui::SodaPop::orchid() : catui::SodaPop::mint(); }
const char* bandName(int b) { return b == 0 ? "LOW" : b == 1 ? "MID" : "HIGH"; }
juce::Colour card()    { return juce::Colour(0xff1f1f4a); }
juce::Colour well()    { return juce::Colour(0xff0f0f2b); }
juce::Colour hotRed()  { return catui::SodaPop::hotPink(); }
juce::Colour title()   { return catui::SodaPop::mistLilac(); }
juce::String dbText(float lin) { const float d = juce::Decibels::gainToDecibels(lin, -100.f); return d <= -99.f ? juce::String("-inf") : juce::String(d, 1); }
float freqToN(float f) { return std::log(juce::jlimit(20.f, 20000.f, f) / 20.f) / std::log(1000.f); }
float nToFreq(float n) { return 20.f * std::pow(1000.f, juce::jlimit(0.f, 1.f, n)); }
// small left and right arrows beside a draggable crossover line
void drawXArrows(juce::Graphics& g, float x, float cy)
{
    juce::Path p; p.addTriangle(x - 3.f, cy, x - 9.f, cy - 5.f, x - 9.f, cy + 5.f); p.addTriangle(x + 3.f, cy, x + 9.f, cy - 5.f, x + 9.f, cy + 5.f);
    g.setColour(juce::Colours::white.withAlpha(0.95f)); g.fillPath(p);
}

// Look and feel: the cat one plus neon rotary knobs (colour comes from the slider's fill colour).
class ScaredyLAF : public catui::CatLookAndFeel
{
public:
    ScaredyLAF()
    {
        setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111130));
        setColour(juce::ComboBox::textColourId, Colours::text());
        setColour(juce::ComboBox::outlineColourId, Colours::dim().withAlpha(0.35f));
        setColour(juce::ComboBox::arrowColourId, Colours::accent());
        setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff161638));
        setColour(juce::PopupMenu::textColourId, Colours::text());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, catui::SodaPop::periwinkle());
        setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff0b0b20));
        setColour(juce::TooltipWindow::textColourId, Colours::text());
        setColour(juce::TooltipWindow::outlineColourId, catui::SodaPop::periwinkle());
        setColour(juce::Slider::textBoxTextColourId, Colours::text());
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::TextButton::buttonColourId, juce::Colour(0xff111130));
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
    {
        const auto neon = s.findColour(juce::Slider::rotarySliderFillColourId);
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3.f);
        const float d = juce::jmin(r.getWidth(), r.getHeight()); r = r.withSizeKeepingCentre(d, d);
        const float angle = a0 + pos * (a1 - a0), cx = r.getCentreX(), cy = r.getCentreY(), R = d * 0.5f;
        const int ticks = 21;                                    // tick ring: lit up to the value
        for (int i = 0; i < ticks; ++i)
        {
            const float t = (float) i / (float) (ticks - 1), a = a0 + t * (a1 - a0), c = std::cos(a - juce::MathConstants<float>::halfPi), sn = std::sin(a - juce::MathConstants<float>::halfPi);
            g.setColour(t <= pos + 0.001f ? neon : neon.withAlpha(0.22f));
            g.drawLine(cx + c * (R * 0.80f), cy + sn * (R * 0.80f), cx + c * (R * 0.96f), cy + sn * (R * 0.96f), juce::jmax(1.5f, d * 0.022f));
        }
        const auto body = r.reduced(d * 0.17f);
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff34346a), body.getX(), body.getY(), juce::Colour(0xff15153a), body.getRight(), body.getBottom(), false));
        g.fillEllipse(body);
        g.setColour(neon.withAlpha(0.55f)); g.drawEllipse(body, 1.5f);
        juce::Path tick; tick.addRoundedRectangle(-1.6f, -body.getHeight() * 0.5f + 3.f, 3.2f, body.getHeight() * 0.28f, 1.2f);
        g.setColour(juce::Colours::white); g.fillPath(tick, juce::AffineTransform::rotation(angle).translated(cx, cy));
    }
};

// ---------- small controls bound to parameters (each shows its own state, no sliders needed) ----------
class ChoiceStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChoiceStrip(juce::RangedAudioParameter& p, juce::StringArray labels, juce::Colour on, std::function<void()> changed = {})
        : items(std::move(labels)), col(on), cb(std::move(changed)), att(p, [this](float v) { idx = (int) std::lround(v); repaint(); if (cb) cb(); }) { att.sendInitialUpdate(); }
    int index() const { return idx; }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 5.f);
        const float w = r.getWidth() / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            auto b = juce::Rectangle<float>(r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced(2.f);
            if (i == idx) { g.setColour(col); g.fillRoundedRectangle(b, 4.f); }
            g.setColour(i == idx ? juce::Colour(0xff0b0b20) : Colours::text()); g.setFont(juce::FontOptions(12.f, juce::Font::bold));
            g.drawText(items[i], b.toNearestInt(), juce::Justification::centred);
        }
        g.setColour(Colours::dim().withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), 5.f, 1.f);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit(0, items.size() - 1, (int) (e.position.x / (getWidth() / (float) items.size())));
        att.setValueAsCompleteGesture((float) i);
    }
private:
    juce::StringArray items; juce::Colour col; std::function<void()> cb; int idx = 0;
    juce::ParameterAttachment att;
};

// on/off parameter drawn as a check box (label to the right) or as a pill
class ToggleChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToggleChip(juce::RangedAudioParameter& p, juce::String text, juce::Colour on, bool pill = false, std::function<void()> changed = {})
        : label(std::move(text)), col(on), isPill(pill), cb(std::move(changed)), att(p, [this](float v) { state = v > 0.5f; repaint(); if (cb) cb(); }) { att.sendInitialUpdate(); }
    bool isOn() const { return state; }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (isPill)
        {
            g.setColour(state ? col : juce::Colour(0xff111130)); g.fillRoundedRectangle(r.reduced(1.f), 5.f);
            g.setColour(state ? col.brighter(0.2f) : Colours::dim().withAlpha(0.4f)); g.drawRoundedRectangle(r.reduced(1.5f), 5.f, 1.2f);
            g.setColour(state ? juce::Colour(0xff0b0b20) : Colours::text()); g.setFont(juce::FontOptions(12.f, juce::Font::bold));
            g.drawText(label, r.toNearestInt(), juce::Justification::centred);
            return;
        }
        auto box = r.withWidth(16.f).withSizeKeepingCentre(16.f, 16.f).withX(r.getX() + 1.f);
        g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(box, 3.f);
        g.setColour(col); g.drawRoundedRectangle(box, 3.f, 1.5f);
        if (state) { juce::Path t; t.startNewSubPath(box.getX() + 3.5f, box.getCentreY()); t.lineTo(box.getX() + 7.f, box.getBottom() - 4.f); t.lineTo(box.getRight() - 3.f, box.getY() + 4.f); g.strokePath(t, juce::PathStrokeType(2.2f)); }
        g.setColour(state ? col : Colours::text()); g.setFont(juce::FontOptions(13.f));
        g.drawText(label, r.withTrimmedLeft(24.f).toNearestInt(), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent&) override { att.setValueAsCompleteGesture(state ? 0.f : 1.f); }
private:
    juce::String label; juce::Colour col; bool isPill; std::function<void()> cb; bool state = false;
    juce::ParameterAttachment att;
};

// choice parameter shown as text; a click steps to the next item (right click: back)
class CycleChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    CycleChip(juce::RangedAudioParameter& p, std::function<juce::String(int)> text, juce::Colour c, int count)
        : param(p), fmt(std::move(text)), col(c), n(count), att(p, [this](float v) { idx = (int) std::lround(v); repaint(); }) { att.sendInitialUpdate(); }
    void paint(juce::Graphics& g) override
    {
        g.setColour(col); g.setFont(juce::FontOptions(12.5f)); g.drawText(fmt(idx), getLocalBounds(), juce::Justification::centredRight);
    }
    void mouseDown(const juce::MouseEvent& e) override { att.setValueAsCompleteGesture((float) ((idx + (e.mods.isPopupMenu() ? n - 1 : 1)) % n)); }
private:
    juce::RangedAudioParameter& param; std::function<juce::String(int)> fmt; juce::Colour col; int n, idx = 0;
    juce::ParameterAttachment att;
};

// number you drag up and down (double click resets, wheel nudges)
class ValueChip : public juce::Component, public juce::SettableTooltipClient
{
public:
    ValueChip(juce::RangedAudioParameter& p, juce::String prefix, std::function<juce::String(float)> text, juce::Colour c, bool boxed = false)
        : param(p), pre(std::move(prefix)), fmt(std::move(text)), col(c), box(boxed), att(p, [this](float v) { val = v; repaint(); }) { att.sendInitialUpdate(); }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (box) { g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 5.f); g.setColour(col.withAlpha(0.7f)); g.drawRoundedRectangle(r.reduced(0.5f), 5.f, 1.2f); }
        const auto vtxt = fmt(val); g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        const float vw = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), vtxt) + 2.f;
        g.setColour(col); g.drawText(vtxt, r.withTrimmedRight(8.f).toNearestInt(), juce::Justification::centredRight);
        g.setColour(Colours::dim()); g.setFont(juce::FontOptions(11.f)); g.drawText(pre, r.withTrimmedLeft(8.f).withTrimmedRight(vw + 10.f).toNearestInt(), juce::Justification::centredLeft);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.getNumberOfClicks() > 1) { att.setValueAsCompleteGesture(param.convertFrom0to1(param.getDefaultValue())); return; }
        gesture = true; att.beginGesture(); startY = e.y; startN = param.getValue();
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! gesture) return;
        const float k = e.mods.isShiftDown() ? 800.f : 220.f;
        att.setValueAsPartOfGesture(param.convertFrom0to1(juce::jlimit(0.f, 1.f, startN + (float) (startY - e.y) / k)));
    }
    void mouseUp(const juce::MouseEvent&) override { if (gesture) { att.endGesture(); gesture = false; } }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        att.setValueAsCompleteGesture(param.convertFrom0to1(juce::jlimit(0.f, 1.f, param.getValue() + (w.deltaY > 0 ? 0.02f : -0.02f))));
    }
private:
    juce::RangedAudioParameter& param; juce::String pre; std::function<juce::String(float)> fmt; juce::Colour col; bool box; float val = 0.f, startN = 0.f; int startY = 0; bool gesture = false;
    juce::ParameterAttachment att;
};

class SoloBtn : public juce::Component, public juce::SettableTooltipClient
{
public:
    SoloBtn(juce::RangedAudioParameter& p, int band) : b(band), att(p, [this](float v) { on = (int) std::lround(v) == b + 1; repaint(); }) { att.sendInitialUpdate(); }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(on ? bandCol(b) : juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 4.f);
        g.setColour(bandCol(b).withAlpha(0.8f)); g.drawRoundedRectangle(r, 4.f, 1.f);
        g.setColour(on ? juce::Colour(0xff0b0b20) : Colours::text()); g.setFont(juce::FontOptions(11.f, juce::Font::bold)); g.drawText("S", r.toNearestInt(), juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent&) override { att.setValueAsCompleteGesture(on ? 0.f : (float) (b + 1)); }
private:
    int b; bool on = false; juce::ParameterAttachment att;
};

// shared by the crossover graph and the spectrum: pick and drag the two crossover lines
struct XoverDrag
{
    explicit XoverDrag(Scaredy& p)
        : lo(*p.apvts.getParameter("xlo")), hi(*p.apvts.getParameter("xhi")), aLo(lo, [](float) {}), aHi(hi, [](float) {}) {}
    float freq(int w) const { return (w == 0 ? lo : hi).convertFrom0to1((w == 0 ? lo : hi).getValue()); }
    void begin(int w) { which = w; (w == 0 ? aLo : aHi).beginGesture(); }
    void drag(float f) { auto& p = which == 0 ? lo : hi; auto& a = which == 0 ? aLo : aHi; a.setValueAsPartOfGesture(juce::jlimit(p.convertFrom0to1(0.f), p.convertFrom0to1(1.f), f)); }
    void end() { if (which >= 0) (which == 0 ? aLo : aHi).endGesture(); which = -1; }
    void reset(int w) { auto& p = w == 0 ? lo : hi; (w == 0 ? aLo : aHi).setValueAsCompleteGesture(p.convertFrom0to1(p.getDefaultValue())); }   // double click: back to the default frequency
    int pick(float xPix, float x0, float w, float tol) const
    {
        const float x1 = x0 + w * freqToN(freq(0)), x2 = x0 + w * freqToN(freq(1));
        const float d1 = std::abs(xPix - x1), d2 = std::abs(xPix - x2);
        if (std::min(d1, d2) > tol) return -1;
        return d1 <= d2 ? 0 : 1;
    }
    juce::RangedAudioParameter &lo, &hi; juce::ParameterAttachment aLo, aHi; int which = -1;
};

class CrossGraph : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit CrossGraph(Scaredy& p) : proc(p), xd(p) {}
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft(32.f).withTrimmedTop(6.f).withTrimmedBottom(14.f).withTrimmedRight(4.f); }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(catfx::viz::bg());
        const auto r = plot(); const double f1 = xd.freq(0), f2 = xd.freq(1);
        const int slope = juce::jlimit(0, 2, (int) std::lround(proc.get("slope"))); const bool multi = proc.get("multiband") > 0.5f;
        catfx::viz::dbAxis(g, r, -36.f, 0.f, 12.f); catfx::viz::freqAxis(g, r);
        auto yOf = [&](double m) { const double db = juce::jlimit(-36.0, 0.0, 20.0 * std::log10(std::max(1e-6, m))); return (float) (r.getBottom() - (db + 36.0) / 36.0 * r.getHeight()); };
        for (int b = 0; b < 3; ++b)
        {
            juce::Path fill, line; bool first = true;
            for (float x = 0.f; x <= r.getWidth(); x += 2.f)
            {
                const double f = nToFreq(x / r.getWidth());
                double m = 1.0;
                if (multi) m = b == 0 ? lrMag(f, f1, slope, false) : b == 1 ? lrMag(f, f1, slope, true) * lrMag(f, f2, slope, false) : lrMag(f, f2, slope, true);
                else if (b != 1) continue;
                const float y = yOf(m);
                if (first) { fill.startNewSubPath(r.getX() + x, r.getBottom()); fill.lineTo(r.getX() + x, y); line.startNewSubPath(r.getX() + x, y); first = false; }
                else { fill.lineTo(r.getX() + x, y); line.lineTo(r.getX() + x, y); }
            }
            if (first) continue;
            fill.lineTo(r.getRight(), r.getBottom()); fill.closeSubPath();
            g.setColour(bandCol(b).withAlpha(0.28f)); g.fillPath(fill);
            g.setColour(bandCol(b)); g.strokePath(line, juce::PathStrokeType(1.6f));
        }
        if (multi)
        {
            g.setFont(juce::FontOptions(10.f, juce::Font::bold));
            for (int w = 0; w < 2; ++w)
            {
                const double f = w == 0 ? f1 : f2; const float x = r.getX() + r.getWidth() * freqToN((float) f);
                g.setColour(juce::Colours::white.withAlpha(0.75f)); g.drawVerticalLine((int) x, r.getY(), r.getBottom()); drawXArrows(g, x, r.getCentreY());
                g.setColour(juce::Colours::white); g.drawText(f >= 1000.0 ? juce::String(f / 1000.0, 2) + "k" : juce::String((int) std::lround(f)) + "Hz", (int) x - 24, (int) r.getBottom() - 14, 48, 12, juce::Justification::centred);
            }
        }
    }
    void mouseMove(const juce::MouseEvent& e) override { setMouseCursor(proc.get("multiband") > 0.5f && xd.pick(e.position.x, plot().getX(), plot().getWidth(), 8.f) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown(const juce::MouseEvent& e) override { const int w = xd.pick(e.position.x, plot().getX(), plot().getWidth(), 10.f); if (w >= 0) { if (e.getNumberOfClicks() > 1) xd.reset(w); else xd.begin(w); } }
    void mouseDrag(const juce::MouseEvent& e) override { if (xd.which >= 0) xd.drag(nToFreq((e.position.x - plot().getX()) / plot().getWidth())); }
    void mouseUp(const juce::MouseEvent&) override { xd.end(); }
private:
    Scaredy& proc; XoverDrag xd;
};

// ---------- the envelope lanes ----------
class EnvEditor : public juce::Component, public juce::SettableTooltipClient
{
public:
    enum Tool { Select, RampUp, RampDown, Hold, Step };
    explicit EnvEditor(Scaredy& p) : proc(p) { setWantsKeyboardFocus(true); refresh(); }
    void refresh() { if (internal) return; for (int i = 0; i < 3; ++i) cur[(size_t) i] = proc.curve(i); sel.clear(); repaint(); }
    int activeLane = 1; Tool tool = Select; std::function<void()> onLaneChanged;

    float plotX() const { return 36.f; }
    float plotW() const { return (float) getWidth() - plotX() - 4.f; }
    float laneH() const { return (float) getHeight() / 3.f; }
    juce::Rectangle<float> laneBox(int b) const { return { plotX(), (float) (2 - b) * laneH(), plotW(), laneH() }; }
    juce::Rectangle<float> inner(int b) const { return laneBox(b).reduced(0.f, 9.f); }
    juce::Point<float> ptPos(int b, const Pt& p) const { const auto in = inner(b); return { in.getX() + p.x * in.getWidth(), in.getBottom() - p.y * in.getHeight() }; }
    int gridDiv() const { static const int d[4] = { 4, 8, 16, 32 }; return d[juce::jlimit(0, 3, (int) std::lround(proc.get("grid")))]; }
    bool snapOn() const { return proc.get("snap") > 0.5f; }
    bool locked() const { return (bool) proc.getProp("locked", false); }
    float snapX(float x) const { if (! snapOn()) return juce::jlimit(0.f, 1.f, x); const float g = (float) gridDiv(); return juce::jlimit(0.f, 1.f, std::round(x * g) / g); }
    int laneAt(float y) const { return 2 - juce::jlimit(0, 2, (int) (y / laneH())); }
    float xN(int b, float px) const { const auto in = inner(b); return juce::jlimit(0.f, 1.f, (px - in.getX()) / in.getWidth()); }
    float yN(int b, float py) const { const auto in = inner(b); return juce::jlimit(0.f, 1.f, (in.getBottom() - py) / in.getHeight()); }

    void commit(int b)
    {
        internal = true;
        const bool linked = proc.get("linked") > 0.5f;
        if (linked) for (int i = 0; i < 3; ++i) cur[(size_t) i] = cur[(size_t) b];
        proc.setCurve(b, cur[(size_t) b], linked);
        internal = false; repaint();
    }
    // toolbar and shape actions
    void setTool(Tool t) { tool = t; }
    void deleteSelection()
    {
        if (locked()) return;
        auto& c = cur[(size_t) activeLane];
        if (sel.empty()) { c = { { 0.f, 1.f, 0.f }, { 1.f, 1.f, 0.f } }; }
        else { Curve r; for (size_t i = 0; i < c.size(); ++i) if (! sel.count((int) i)) r.push_back(c[i]); if (r.size() < 2) r = { { 0.f, 1.f, 0.f }, { 1.f, 1.f, 0.f } }; c = r; sel.clear(); }
        commit(activeLane);
    }
    void selectAll() { sel.clear(); for (size_t i = 0; i < cur[(size_t) activeLane].size(); ++i) sel.insert((int) i); repaint(); }
    void nudge(int dir)
    {
        if (locked()) return;
        auto& c = cur[(size_t) activeLane]; const float step = (float) dir / (float) gridDiv();
        if (sel.empty()) shiftCurve(c, step);
        else
        {
            std::vector<int> idx(sel.begin(), sel.end()); if (dir > 0) std::reverse(idx.begin(), idx.end());
            for (int i : idx) { const int n = (int) c.size(); const float lo = i > 0 ? c[(size_t) i - 1].x + kMinGap : 0.f, hi = i < n - 1 ? c[(size_t) i + 1].x - kMinGap : 1.f; c[(size_t) i].x = juce::jlimit(lo, hi, c[(size_t) i].x + step); }
        }
        commit(activeLane);
    }
    void applyShape(int id, unsigned seed = 1)
    {
        if (locked()) return;
        static const float lo[3] = { 0.f, 0.1f, 0.45f };
        cur[(size_t) activeLane] = shapeCurve(id, lo[activeLane], seed); sel.clear(); commit(activeLane);
    }
    void applyOp(int op)
    {
        if (locked()) return;
        auto& c = cur[(size_t) activeLane];
        if (op == 0) reverseCurve(c); else if (op == 1) flipCurve(c);
        else if (op == 2) { const auto src = c; for (int i = 0; i < 3; ++i) cur[(size_t) i] = src; internal = true; proc.setCurve(activeLane, src, true); internal = false; repaint(); return; }
        sel.clear(); commit(activeLane);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(catfx::viz::bg());
        const float phase = proc.engine.mPhase.load(std::memory_order_relaxed); const int G = gridDiv();
        for (int b = 0; b < 3; ++b)
        {
            const auto box = laneBox(b), in = inner(b); const auto col = bandCol(b); const auto& cv = cur[(size_t) b];
            g.setColour(col.withAlpha(b == activeLane ? 0.075f : 0.035f)); g.fillRect(box);
            for (int k = 0; k <= G; ++k)
            {
                const float x = box.getX() + box.getWidth() * (float) k / (float) G; const bool beat = k % std::max(1, G / 4) == 0;
                g.setColour(juce::Colours::white.withAlpha(beat ? 0.16f : 0.055f)); g.drawVerticalLine((int) x, box.getY(), box.getBottom());
            }
            static const float gs[5] = { 1.f, 0.5f, 0.25f, 0.1f, 0.f }; static const char* gl[5] = { "0 dB", "-6", "-12", "-20", "-inf" };
            g.setFont(juce::FontOptions(9.5f));
            for (int k = 0; k < 5; ++k)
            {
                const float y = in.getBottom() - gs[k] * in.getHeight();
                g.setColour(col.withAlpha(0.15f)); g.drawHorizontalLine((int) y, box.getX(), box.getRight());
                g.setColour(col.withAlpha(0.95f)); g.drawText(gl[k], 0, (int) y - 5, 34, 10, juce::Justification::right);
            }
            // the band folded over the cycle: pink = what the ducking removes, band colour = what is left (before: full bar, after: coloured bar)
            { const float cw = box.getWidth() / 256.f, hh = in.getHeight() * 0.5f, cy = in.getCentreY(), w = std::max(1.f, cw - 0.3f);
              for (int pass = 0; pass < 2; ++pass)
              {
                  g.setColour(pass == 0 ? hotRed().withAlpha(0.5f) : col.withAlpha(0.55f));
                  for (int c = 0; c < 256; ++c)
                  {
                      const float v = (pass == 0 ? proc.engine.wavePre[b][c] : proc.engine.wavePost[b][c]).load(std::memory_order_relaxed);
                      const float a = std::sqrt(std::min(1.f, v)) * hh; if (a > 0.2f) g.fillRect(box.getX() + cw * (float) c, cy - a, w, a * 2.f);
                  }
              } }
            g.saveState(); g.reduceClipRegion(box.toNearestInt());
            juce::Path line, fill;
            for (int i = 0; i <= 200; ++i)
            {
                const float x = (float) i / 200.f, y = evalCurve(cv, x); const float px = in.getX() + x * in.getWidth(), py = in.getBottom() - y * in.getHeight();
                if (i == 0) { line.startNewSubPath(px, py); fill.startNewSubPath(px, in.getBottom()); fill.lineTo(px, py); } else { line.lineTo(px, py); fill.lineTo(px, py); }
            }
            fill.lineTo(in.getRight(), in.getBottom()); fill.closeSubPath();
            g.setColour(col.withAlpha(0.20f)); g.fillPath(fill);
            g.setColour(col); g.strokePath(line, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved));
            for (size_t i = 0; i < cv.size(); ++i)
            {
                const auto pp = ptPos(b, cv[i]); const bool s = b == activeLane && sel.count((int) i) > 0;
                g.setColour(s ? juce::Colours::white : well()); g.fillEllipse(pp.x - 5.f, pp.y - 5.f, 10.f, 10.f);
                g.setColour(col); g.drawEllipse(pp.x - 5.f, pp.y - 5.f, 10.f, 10.f, 2.f);
            }
            // playhead and the current value on this lane
            const float hx = in.getX() + phase * in.getWidth(), hy = in.getBottom() - evalCurve(cv, phase) * in.getHeight();
            g.setColour(hotRed().withAlpha(0.9f)); g.drawVerticalLine((int) hx, box.getY(), box.getBottom());
            g.setColour(juce::Colours::white); g.fillEllipse(hx - 3.5f, hy - 3.5f, 7.f, 7.f);
            g.restoreState();
            g.setColour(col); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(bandName(b), (int) box.getX() + 6, (int) box.getY() + 3, 60, 12, juce::Justification::left);
            g.setColour(juce::Colours::black.withAlpha(0.7f)); g.drawHorizontalLine((int) box.getBottom() - 1, box.getX(), box.getRight());
            if (b == activeLane) { g.setColour(col.withAlpha(0.55f)); g.drawRect(box.reduced(0.5f), 1.f); }
        }
        if (drag == Draw && tool != Select)
        {
            const auto in = inner(dragLane); g.setColour(juce::Colours::white.withAlpha(0.85f));
            const float dl[2] = { 5.f, 4.f }; g.drawDashedLine(juce::Line<float>(in.getX() + drawX0 * in.getWidth(), in.getBottom() - drawY0 * in.getHeight(), in.getX() + drawX1 * in.getWidth(), in.getBottom() - drawY1 * in.getHeight()), dl, 2, 1.5f);
        }
        if (locked()) { g.setColour(juce::Colours::black.withAlpha(0.35f)); g.fillRect(getLocalBounds()); g.setColour(Colours::accent()); g.setFont(juce::FontOptions(13.f, juce::Font::bold)); g.drawText("LOCKED", getLocalBounds().removeFromTop(20).removeFromRight(80), juce::Justification::centredRight); }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        const int b = laneAt(e.position.y);
        if (b != activeLane) { activeLane = b; sel.clear(); if (onLaneChanged) onLaneChanged(); }
        if (locked()) { repaint(); return; }
        auto& c = cur[(size_t) b]; int hit = -1; float best = 11.f;
        for (size_t i = 0; i < c.size(); ++i) { const float d = ptPos(b, c[i]).getDistanceFrom(e.position); if (d < best) { best = d; hit = (int) i; } }
        drag = None; dragLane = b; downPos = e.position; orig = c;
        if (hit >= 0)
        {
            if (e.mods.isPopupMenu()) { if (c.size() > 2) { c.erase(c.begin() + hit); sel.clear(); commit(b); } return; }
            if (e.mods.isShiftDown()) { if (sel.count(hit)) sel.erase(hit); else sel.insert(hit); }
            else if (! sel.count(hit)) { sel.clear(); sel.insert(hit); }
            pressed = hit; drag = Points; repaint(); return;
        }
        if (tool != Select)
        {
            drag = Draw; drawX0 = drawX1 = snapX(xN(b, e.position.x)); drawY0 = drawY1 = yN(b, e.position.y); repaint(); return;
        }
        if (e.getNumberOfClicks() > 1)
        {
            const int i = insertPoint(c, snapX(xN(b, e.position.x))); sel.clear(); sel.insert(i); commit(b); return;
        }
        if (nearCurve(b, e.position) || e.mods.isAltDown() || e.mods.isCtrlDown())   // drag the curve itself to bend it
        {
            const float x = xN(b, e.position.x); segIdx = 0;
            for (size_t i = 0; i + 1 < c.size(); ++i) if (c[i + 1].x <= x) segIdx = (int) i + 1;
            if (segIdx + 1 < (int) c.size()) { drag = Curve_; repaint(); return; }
        }
        sel.clear(); repaint();
    }
    bool nearCurve(int b, juce::Point<float> pos) const
    {
        const auto& c = cur[(size_t) b]; const float x = xN(b, pos.x);
        for (size_t i = 0; i < c.size(); ++i) if (ptPos(b, c[i]).getDistanceFrom(pos) < 11.f) return false;
        return std::abs(ptPos(b, Pt { x, evalCurve(c, x), 0.f }).y - pos.y) < 13.f;
    }
    void mouseMove(const juce::MouseEvent& e) override
    {
        setMouseCursor(! locked() && tool == Select && nearCurve(laneAt(e.position.y), e.position) ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::NormalCursor);
    }
    Curve laneCurve() const { return cur[(size_t) activeLane]; }
    void applyCurve(Curve c)
    {
        if (locked()) return;
        sanitize(c); cur[(size_t) activeLane] = std::move(c); sel.clear(); commit(activeLane);
    }
    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (locked() || drag == None) return;
        const int b = dragLane; auto& c = cur[(size_t) b]; const auto in = inner(b);
        if (drag == Points)
        {
            float dxn = (e.position.x - downPos.x) / in.getWidth(); const float dyn = -(e.position.y - downPos.y) / in.getHeight();
            if (pressed >= 0 && pressed < (int) orig.size() && snapOn() && ! e.mods.isCtrlDown()) dxn = snapX(orig[(size_t) pressed].x + dxn) - orig[(size_t) pressed].x;
            std::vector<int> idx(sel.begin(), sel.end()); if (dxn > 0.f) std::reverse(idx.begin(), idx.end());
            const int n = (int) c.size();
            for (int i : idx)
            {
                if (i < 0 || i >= n || i >= (int) orig.size()) continue;
                const float lo = i > 0 ? c[(size_t) i - 1].x + kMinGap : 0.f, hi = i < n - 1 ? c[(size_t) i + 1].x - kMinGap : 1.f;
                c[(size_t) i].x = juce::jlimit(lo, std::max(lo, hi), orig[(size_t) i].x + dxn);
                float y = juce::jlimit(0.f, 1.f, orig[(size_t) i].y + dyn); if (y < 0.02f) y = 0.f; if (y > 0.98f) y = 1.f; c[(size_t) i].y = y;
            }
            commit(b);
        }
        else if (drag == Curve_ && segIdx >= 0 && segIdx + 1 < (int) c.size() && segIdx < (int) orig.size())
        {
            const bool rising = orig[(size_t) segIdx + 1].y > orig[(size_t) segIdx].y; const float up = (downPos.y - e.position.y) / (in.getHeight() * 0.5f);
            c[(size_t) segIdx].c = juce::jlimit(-1.f, 1.f, orig[(size_t) segIdx].c + (rising ? -up : up)); commit(b);
        }
        else if (drag == Draw) { drawX1 = snapX(xN(b, e.position.x)); drawY1 = yN(b, e.position.y); repaint(); }
    }
    void mouseUp(const juce::MouseEvent&) override
    {
        if (drag == Draw && ! locked()) applyDraw(dragLane);
        drag = None;
    }
    bool keyPressed(const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { deleteSelection(); return true; }
        if (k == juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0)) { selectAll(); return true; }
        if (k == juce::KeyPress::leftKey) { nudge(-1); return true; }
        if (k == juce::KeyPress::rightKey) { nudge(1); return true; }
        return false;
    }
private:
    enum Drag { None, Points, Curve_, Draw };
    void applyDraw(int b)
    {
        auto& c = cur[(size_t) b]; const float xa = std::min(drawX0, drawX1), xb = std::max(drawX0, drawX1);
        if (xb - xa < 0.01f) return;
        const bool leftToRight = drawX1 >= drawX0; const float ya = leftToRight ? drawY0 : drawY1, yb = leftToRight ? drawY1 : drawY0;
        const float pad = tool == Step ? 0.008f : 0.f; Curve r;
        for (auto& p : c) if (p.x < xa - 0.001f || p.x > xb + 0.001f + pad) r.push_back(p);
        const float lo = std::min(ya, yb), hi = std::max(ya, yb);
        if (tool == RampUp) { r.push_back({ xa, lo, 0.f }); r.push_back({ xb, hi, 0.f }); }
        else if (tool == RampDown) { r.push_back({ xa, hi, 0.f }); r.push_back({ xb, lo, 0.f }); }
        else if (tool == Hold) { r.push_back({ xa, ya, 0.f }); r.push_back({ xb, ya, 0.f }); }
        else if (tool == Step) { r.push_back({ xa, ya, 0.f }); r.push_back({ xb, ya, 0.f }); r.push_back({ xb + 0.008f, yb, 0.f }); }
        c = r; sanitize(c); sel.clear(); commit(b);
    }
    Scaredy& proc; std::array<Curve, 3> cur; std::set<int> sel; Curve orig; bool internal = false;
    Drag drag = None; int dragLane = 1, pressed = -1, segIdx = -1; juce::Point<float> downPos; float drawX0 = 0, drawX1 = 0, drawY0 = 0, drawY1 = 0;
public:
    void notifyExternal() { refresh(); }
};

// ---------- the shape glyph row and the shape library ----------
// one line of the library dropdown: a thumbnail of the curve and its name
class ShapeThumb : public juce::PopupMenu::CustomComponent
{
public:
    ShapeThumb(Curve c, juce::String n, juce::Colour col) : juce::PopupMenu::CustomComponent(true), cv(std::move(c)), name(std::move(n)), colour(col) {}
    void getIdealSize(int& w, int& h) override { w = 220; h = 40; }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (isItemHighlighted()) { g.setColour(catui::SodaPop::periwinkle().withAlpha(0.35f)); g.fillRoundedRectangle(r.reduced(2.f), 5.f); }
        auto t = r.removeFromLeft(84.f).reduced(6.f, 4.f);
        g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(t, 4.f); g.setColour(Colours::dim().withAlpha(0.4f)); g.drawRoundedRectangle(t.reduced(0.5f), 4.f, 1.f);
        juce::Path p; const auto in = t.reduced(6.f, 5.f);
        for (int k = 0; k <= 48; ++k) { const float x = (float) k / 48.f, y = evalCurve(cv, x); const float px = in.getX() + x * in.getWidth(), py = in.getBottom() - y * in.getHeight(); if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
        g.setColour(colour); g.strokePath(p, juce::PathStrokeType(1.7f));
        g.setColour(Colours::text()); g.setFont(juce::FontOptions(13.f)); g.drawText(name, r.reduced(6.f, 0.f).toNearestInt(), juce::Justification::centredLeft);
    }
private:
    Curve cv; juce::String name; juce::Colour colour;
};

class ShapeRow : public juce::Component, public juce::SettableTooltipClient
{
public:
    ShapeRow(EnvEditor& e, Scaredy& p) : env(e), proc(p) {}
    static constexpr int kRight = 160;   // [Shapes v] [+] [-]
    int glyphW() const { return (int) ((float) (getWidth() - kRight - 8 - 7 * 5) / 8.f); }
    juce::Rectangle<float> libBox() const { return { (float) getWidth() - (float) kRight, 0.f, 80.f, (float) getHeight() }; }
    juce::Rectangle<float> addBox() const { return { (float) getWidth() - 76.f, 0.f, 36.f, (float) getHeight() }; }
    juce::Rectangle<float> delBox() const { return { (float) getWidth() - 36.f, 0.f, 36.f, (float) getHeight() }; }
    void paint(juce::Graphics& g) override
    {
        const int n = 8; const float w = (float) glyphW();
        const auto col = bandCol(env.activeLane);
        auto chip = [&](juce::Rectangle<float> b) { g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(b, 5.f); g.setColour(Colours::dim().withAlpha(0.4f)); g.drawRoundedRectangle(b.reduced(0.5f), 5.f, 1.f); };
        for (int i = 0; i < n; ++i)
        {
            auto b = juce::Rectangle<float>((float) i * (w + 5.f), 0.f, w, (float) getHeight()); chip(b);
            const auto cv = shapeCurve(i, 0.f); juce::Path p; const auto in = b.reduced(9.f, 7.f);
            for (int k = 0; k <= 40; ++k) { const float x = (float) k / 40.f, y = evalCurve(cv, x); const float px = in.getX() + x * in.getWidth(), py = in.getBottom() - y * in.getHeight(); if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py); }
            g.setColour(col); g.strokePath(p, juce::PathStrokeType(1.8f));
        }
        chip(libBox()); g.setColour(Colours::text()); g.setFont(juce::FontOptions(12.f)); g.drawText("Shapes", libBox().withTrimmedRight(14.f).toNearestInt(), juce::Justification::centred);
        { const auto a = libBox(); juce::Path tri; tri.addTriangle(a.getRight() - 16.f, a.getCentreY() - 2.f, a.getRight() - 8.f, a.getCentreY() - 2.f, a.getRight() - 12.f, a.getCentreY() + 3.f); g.setColour(Colours::accent()); g.fillPath(tri); }
        chip(addBox()); chip(delBox());
        g.setColour(catui::SodaPop::mint()); g.setFont(juce::FontOptions(20.f, juce::Font::bold)); g.drawText("+", addBox().toNearestInt(), juce::Justification::centred);
        g.setColour(hotRed()); g.drawText("-", delBox().toNearestInt(), juce::Justification::centred);
    }
    void mouseDown(const juce::MouseEvent& e) override
    {
        if (libBox().contains(e.position)) { showLibrary(); return; }
        if (addBox().contains(e.position)) { askAdd(); return; }
        if (delBox().contains(e.position)) { showDelete(); return; }
        const int i = (int) (e.position.x / ((float) glyphW() + 5.f)); if (i >= 0 && i < 8 && e.position.x < (float) getWidth() - (float) kRight) env.applyShape(i);
    }
private:
    void showLibrary()
    {
        juce::PopupMenu m; const auto col = bandCol(env.activeLane);
        m.addSectionHeader("Built in");
        for (int i = 0; i < kNumShapes; ++i) m.addCustomItem(100 + i, std::make_unique<ShapeThumb>(shapeCurve(i, 0.f), shapeName(i), col));
        const auto user = proc.userShapeNames();
        if (user.size() > 0)
        {
            m.addSeparator(); m.addSectionHeader("My shapes");
            for (int j = 0; j < user.size(); ++j) { Curve c; if (proc.loadShape(user[j], c)) m.addCustomItem(1000 + j, std::make_unique<ShapeThumb>(c, user[j], col)); }
        }
        m.addSeparator(); m.addItem(1, "Reverse"); m.addItem(2, "Flip upside down"); m.addItem(3, "Copy this lane to all lanes"); m.addItem(4, "Random steps (new)");
        juce::Component::SafePointer<ShapeRow> self(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(230), [self, user](int r)
        {
            if (self == nullptr || r == 0) return;
            auto& env = self->env;
            if (r >= 1000) { Curve c; if (r - 1000 < user.size() && self->proc.loadShape(user[r - 1000], c)) env.applyCurve(c); }
            else if (r >= 100) env.applyShape(r - 100);
            else if (r == 1) env.applyOp(0); else if (r == 2) env.applyOp(1); else if (r == 3) env.applyOp(2);
            else if (r == 4) env.applyShape(10, (unsigned) juce::Random::getSystemRandom().nextInt(100000));
        });
    }
    void askAdd()
    {
        auto* w = new juce::AlertWindow("Add shape", "Name for this shape (saved in your shape library):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", "My shape");
        w->addButton("Add", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ShapeRow> self(this); const Curve c = env.laneCurve();
        w->enterModalState(true, juce::ModalCallbackFunction::create([self, w, c](int r) { if (r == 1 && self != nullptr) self->proc.saveShape(w->getTextEditorContents("name"), c); }), true);
    }
    void showDelete()
    {
        const auto user = proc.userShapeNames();
        juce::PopupMenu m; const auto col = bandCol(env.activeLane);
        if (user.size() == 0) m.addItem(-1, "No saved shapes yet", false);
        else { m.addSectionHeader("Delete which shape?"); for (int j = 0; j < user.size(); ++j) { Curve c; if (proc.loadShape(user[j], c)) m.addCustomItem(2000 + j, std::make_unique<ShapeThumb>(c, user[j], col)); } }
        juce::Component::SafePointer<ShapeRow> self(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMinimumWidth(230), [self, user](int r)
        {
            if (self == nullptr || r < 2000 || r - 2000 >= user.size()) return;
            const juce::String name = user[r - 2000];
            auto* w = new juce::AlertWindow("Delete shape", "You are going to delete this shape, are you sure?", juce::MessageBoxIconType::QuestionIcon, self.getComponent());
            w->addButton("Yes", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("No", 0, juce::KeyPress(juce::KeyPress::escapeKey));
            w->enterModalState(true, juce::ModalCallbackFunction::create([self, name](int ans) { if (ans == 1 && self != nullptr) self->proc.deleteShape(name); }), true);
        });
    }
    EnvEditor& env; Scaredy& proc;
};

// icon button for the toolbar
class ToolBtn : public juce::Component, public juce::SettableTooltipClient
{
public:
    ToolBtn(int k, std::function<void()> click) : kind_(k), cb(std::move(click)) {}
    bool on = false;
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(on ? Colours::accent() : juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 5.f);
        g.setColour(on ? Colours::accent() : Colours::dim().withAlpha(0.4f)); g.drawRoundedRectangle(r, 5.f, 1.f);
        g.setColour(on ? juce::Colour(0xff0b0b20) : Colours::text()); const auto c = r.reduced(9.f, 8.f); const float st = 1.8f;
        juce::Path p;
        switch (kind_)
        {
            case 0: p.startNewSubPath(c.getX() + 2, c.getY()); p.lineTo(c.getX() + 2, c.getBottom()); p.lineTo(c.getX() + 6, c.getBottom() - 4); p.lineTo(c.getX() + 10, c.getBottom() + 1); p.lineTo(c.getX() + 7, c.getBottom() - 5); p.lineTo(c.getX() + 12, c.getBottom() - 5); p.closeSubPath(); g.fillPath(p); return;
            case 1: p.startNewSubPath(c.getX(), c.getBottom()); p.lineTo(c.getRight(), c.getY()); break;
            case 2: p.startNewSubPath(c.getX(), c.getY()); p.lineTo(c.getRight(), c.getBottom()); break;
            case 3: p.startNewSubPath(c.getX(), c.getCentreY()); p.lineTo(c.getRight(), c.getCentreY()); break;
            case 4: p.startNewSubPath(c.getX(), c.getBottom()); p.lineTo(c.getCentreX(), c.getBottom()); p.lineTo(c.getCentreX(), c.getY()); p.lineTo(c.getRight(), c.getY()); break;
            case 5: p.addRectangle(c.getX() + 2, c.getY() + 4, c.getWidth() - 4, c.getHeight() - 4); p.addRectangle(c.getX(), c.getY() + 1, c.getWidth(), 2); g.strokePath(p, juce::PathStrokeType(1.5f)); return;
            case 6: { const float d[2] = { 3.f, 2.f }; g.drawDashedLine({ c.getX(), c.getY(), c.getRight(), c.getY() }, d, 2); g.drawDashedLine({ c.getRight(), c.getY(), c.getRight(), c.getBottom() }, d, 2); g.drawDashedLine({ c.getRight(), c.getBottom(), c.getX(), c.getBottom() }, d, 2); g.drawDashedLine({ c.getX(), c.getBottom(), c.getX(), c.getY() }, d, 2); return; }
            case 7: p.startNewSubPath(c.getRight(), c.getCentreY()); p.lineTo(c.getX(), c.getCentreY()); p.startNewSubPath(c.getX() + 5, c.getCentreY() - 5); p.lineTo(c.getX(), c.getCentreY()); p.lineTo(c.getX() + 5, c.getCentreY() + 5); break;
            case 9: { juce::Path body; body.addRoundedRectangle(c.getX() + 1, c.getCentreY() - 1, c.getWidth() - 2, c.getHeight() / 2.f + 2, 2.f); g.fillPath(body);
                      juce::Path sh; sh.addCentredArc(c.getCentreX(), c.getCentreY() - 1, 4.5f, 5.f, 0.f, -juce::MathConstants<float>::halfPi * 2.f, 0.f, true); g.strokePath(sh, juce::PathStrokeType(1.8f)); return; }
            default: p.startNewSubPath(c.getX(), c.getCentreY()); p.lineTo(c.getRight(), c.getCentreY()); p.startNewSubPath(c.getRight() - 5, c.getCentreY() - 5); p.lineTo(c.getRight(), c.getCentreY()); p.lineTo(c.getRight() - 5, c.getCentreY() + 5); break;
        }
        g.strokePath(p, juce::PathStrokeType(st, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseDown(const juce::MouseEvent&) override { if (cb) cb(); }
    int kind() const { return kind_; }
private:
    int kind_; std::function<void()> cb;
};

// two-state strip stored in the plugin state (not automatable): Spectrum | Level
class PropStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    PropStrip(Scaredy& p, juce::String key, juce::StringArray labels, juce::Colour c) : proc(p), k(std::move(key)), items(std::move(labels)), col(c) {}
    int index() const { return (int) proc.getProp(k, 0); }
    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat(); g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 5.f);
        const float w = r.getWidth() / (float) items.size();
        for (int i = 0; i < items.size(); ++i)
        {
            auto b = juce::Rectangle<float>(r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced(2.f);
            if (i == index()) { g.setColour(col); g.fillRoundedRectangle(b, 4.f); }
            g.setColour(i == index() ? juce::Colour(0xff0b0b20) : Colours::text()); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(items[i], b.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent& e) override { proc.setProp(k, juce::jlimit(0, items.size() - 1, (int) (e.position.x / (getWidth() / (float) items.size())))); repaint(); }
private:
    Scaredy& proc; juce::String k; juce::StringArray items; juce::Colour col;
};

// ---------- spectrum / level per band ----------
class SpectrumView : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int N = 2048;
    explicit SpectrumView(Scaredy& p) : proc(p), xd(p)
    {
        for (int i = 0; i < N; ++i) win[i] = 0.5f - 0.5f * std::cos(2.f * juce::MathConstants<float>::pi * (float) i / (float) (N - 1));
        for (auto& s : sm) for (auto& v : s) v = -120.f;
    }
    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().withTrimmedLeft(36.f).withTrimmedTop(3.f).withTrimmedBottom(14.f).withTrimmedRight(4.f); }
    void analyse()
    {
        const unsigned w = proc.wpos.load(std::memory_order_acquire);
        for (int src = 0; src < 3; ++src)
        {
            const unsigned back = (unsigned) N + (src == 0 ? (unsigned) std::min(4000, std::max(0, proc.latency())) : 0u);   // the input is read late by the latency, so it lines up with the output
            for (int i = 0; i < N; ++i) buf[i] = proc.ring[src][(w - back + (unsigned) i) & (unsigned) (Scaredy::kRing - 1)].load(std::memory_order_relaxed) * win[i];
            std::fill(buf + N, buf + 2 * N, 0.f);
            fft.performFrequencyOnlyForwardTransform(buf, true);
            for (int k = 0; k < N / 2; ++k)
            {
                const float db = 20.f * std::log10(std::max(1e-6f, buf[k] / ((float) N * 0.25f)));
                sm[src][k] += (db - sm[src][k]) * (db > sm[src][k] ? 0.55f : 0.12f);
            }
        }
    }
    void paint(juce::Graphics& g) override
    {
        g.fillAll(catfx::viz::bg()); const auto r = plot(); const bool level = (int) proc.getProp("specview", 0) == 1;
        const float f1 = xd.freq(0), f2 = xd.freq(1), x1 = r.getX() + r.getWidth() * freqToN(f1), x2 = r.getX() + r.getWidth() * freqToN(f2);
        const float xs[4] = { r.getX(), x1, x2, r.getRight() };
        for (int b = 0; b < 3; ++b) { g.setColour(bandCol(b).withAlpha(0.07f)); g.fillRect(xs[b], r.getY(), xs[b + 1] - xs[b], r.getHeight()); }
        catfx::viz::dbAxis(g, r, level ? -60.f : -100.f, 0.f, level ? 12.f : 20.f); catfx::viz::freqAxis(g, r);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        for (int b = 0; b < 3; ++b) { g.setColour(bandCol(b)); g.drawText(bandName(b), (int) xs[b] + 4, (int) r.getY() + 1, 40, 11, juce::Justification::left); }
        if (level) paintLevel(g, r, xs); else paintSpectrum(g, r, xs);
        g.setColour(juce::Colours::white.withAlpha(0.8f)); g.drawVerticalLine((int) x1, r.getY(), r.getBottom()); g.drawVerticalLine((int) x2, r.getY(), r.getBottom()); drawXArrows(g, x1, r.getCentreY()); drawXArrows(g, x2, r.getCentreY());
        g.setFont(juce::FontOptions(9.5f)); g.setColour(juce::Colours::white);
        g.drawText(juce::String((int) std::lround(f1)) + " Hz", (int) x1 + 2, (int) r.getBottom() - 12, 50, 11, juce::Justification::left);
        g.drawText(f2 >= 1000.f ? juce::String(f2 / 1000.f, 2) + " kHz" : juce::String((int) f2) + " Hz", (int) x2 + 2, (int) r.getBottom() - 12, 60, 11, juce::Justification::left);
    }
    void mouseMove(const juce::MouseEvent& e) override { setMouseCursor(xd.pick(e.position.x, plot().getX(), plot().getWidth(), 8.f) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor); }
    void mouseDown(const juce::MouseEvent& e) override { const int w = xd.pick(e.position.x, plot().getX(), plot().getWidth(), 10.f); if (w >= 0) { if (e.getNumberOfClicks() > 1) xd.reset(w); else xd.begin(w); } }
    void mouseDrag(const juce::MouseEvent& e) override { if (xd.which >= 0) xd.drag(nToFreq((e.position.x - plot().getX()) / plot().getWidth())); }
    void mouseUp(const juce::MouseEvent&) override { xd.end(); }
private:
    float yAt(int src, float x, juce::Rectangle<float> r) const
    {
        const float binHz = (float) proc.sampleRate() / (float) N;
        const float fa = nToFreq((x - 1.f) / r.getWidth()), fb = nToFreq((x + 1.f) / r.getWidth());
        const int b0 = juce::jlimit(1, N / 2 - 1, (int) std::floor(fa / binHz)), b1 = juce::jlimit(b0, N / 2 - 1, (int) std::ceil(fb / binHz));
        float db = -120.f; for (int k = b0; k <= b1; ++k) db = std::max(db, sm[src][k]);
        return r.getBottom() - juce::jlimit(0.f, 1.f, (db + 100.f) / 100.f) * r.getHeight();   // 0 to -100 dB, the original is never clipped flat
    }
    juce::Path trace(int src, juce::Rectangle<float> r) const
    {
        juce::Path p; bool first = true;
        for (float x = 0.f; x <= r.getWidth(); x += 2.f) { const float y = yAt(src, x, r); if (first) { p.startNewSubPath(r.getX() + x, y); first = false; } else p.lineTo(r.getX() + x, y); }
        return p;
    }
    // dashed: the original sound. Coloured fill: what the ducking takes away from it. Thin line: what comes out.
    void paintSpectrum(juce::Graphics& g, juce::Rectangle<float> r, const float* xs)
    {
        std::vector<float> yo, yb;
        for (float x = 0.f; x <= r.getWidth(); x += 2.f) { const float a = yAt(0, x, r), b = yAt(2, x, r); yo.push_back(a); yb.push_back(std::max(a, b)); }
        juce::Path rem; rem.startNewSubPath(r.getX(), yo[0]);
        for (size_t i = 1; i < yo.size(); ++i) rem.lineTo(r.getX() + 2.f * (float) i, yo[i]);
        for (size_t i = yo.size(); i-- > 0;) rem.lineTo(r.getX() + 2.f * (float) i, yb[i]);
        rem.closeSubPath();
        juce::Path under; under.startNewSubPath(r.getX(), r.getBottom());
        for (size_t i = 0; i < yo.size(); ++i) under.lineTo(r.getX() + 2.f * (float) i, yo[i]);
        under.lineTo(r.getX() + 2.f * (float) (yo.size() - 1), r.getBottom()); under.closeSubPath();   // the whole original spectrum, filled faintly so the view looks full
        for (int b = 0; b < 3; ++b)
        {
            g.saveState(); g.reduceClipRegion(juce::Rectangle<float>(xs[b], r.getY(), xs[b + 1] - xs[b], r.getHeight()).toNearestInt());
            g.setColour(bandCol(b).withAlpha(0.16f)); g.fillPath(under);
            g.setColour(bandCol(b).withAlpha(0.75f)); g.fillPath(rem); g.restoreState();
        }
        g.setColour(juce::Colours::white.withAlpha(0.35f)); g.strokePath(trace(2, r), juce::PathStrokeType(1.0f));
        const float dl[2] = { 4.f, 3.f }; juce::Path d; juce::PathStrokeType(1.4f).createDashedStroke(d, trace(0, r), dl, 2);
        g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillPath(d);
    }
    void paintLevel(juce::Graphics& g, juce::Rectangle<float> r, const float* xs)
    {
        for (int b = 0; b < 3; ++b)
        {
            const float pre = juce::Decibels::gainToDecibels(proc.engine.mPre[b].load(), -80.f), post = juce::Decibels::gainToDecibels(proc.engine.mPost[b].load(), -80.f);
            auto z = juce::Rectangle<float>(xs[b], r.getY(), xs[b + 1] - xs[b], r.getHeight()).reduced(10.f, 0.f);
            auto yOf = [&](float db) { return r.getBottom() - juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f) * r.getHeight(); };
            g.setColour(bandCol(b).withAlpha(0.6f)); g.fillRect(z.withTop(yOf(post)));
            g.setColour(bandCol(b)); g.drawRect(z.withTop(yOf(post)), 1.2f);
            if (pre > post + 0.1f) { g.setColour(hotRed().withAlpha(0.45f)); g.fillRect(z.withTop(yOf(pre)).withBottom(yOf(post))); }   // the part taken away
            g.setColour(juce::Colours::white.withAlpha(0.85f)); const float dl[2] = { 4.f, 3.f }; g.drawDashedLine({ z.getX(), yOf(pre), z.getRight(), yOf(pre) }, dl, 2, 1.4f);
            g.setColour(juce::Colours::white); g.setFont(juce::FontOptions(11.f, juce::Font::bold));
            g.drawText(post <= -79.f ? juce::String("-inf") : juce::String(-std::max(0.f, pre - post), 1) + " dB", z.toNearestInt().withHeight(13).withY((int) r.getY() + 12), juce::Justification::centred);
        }
    }
    Scaredy& proc; XoverDrag xd; juce::dsp::FFT fft { 11 }; float win[N]; float buf[2 * N]; float sm[3][N / 2];
};

// ---------- output meters ----------
class Meters : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit Meters(Scaredy& p) : proc(p) {}
    void paint(juce::Graphics& g) override
    {
        const float top = 18.f, bot = (float) getHeight() - 30.f, h = bot - top;
        auto yDb = [&](float db) { return bot - juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f) * h; };
        g.setFont(juce::FontOptions(10.f)); g.setColour(catfx::viz::green());
        for (float t : { 0.f, -6.f, -12.f, -24.f, -48.f }) { g.drawText(juce::String((int) t), 0, (int) yDb(t) - 5, 30, 10, juce::Justification::right); g.setColour(catfx::viz::grid().brighter(0.4f)); g.drawHorizontalLine((int) yDb(t), 33.f, 98.f); g.setColour(catfx::viz::green()); }
        const float lv[2] = { proc.peakL.load(std::memory_order_relaxed), proc.peakR.load(std::memory_order_relaxed) };
        for (int c = 0; c < 2; ++c)
        {
            const float x = 40.f + (float) c * 26.f, db = juce::Decibels::gainToDecibels(lv[c], -80.f);
            g.setColour(well()); g.fillRect(x, top, 18.f, h);
            g.setColour(db > -1.f ? hotRed() : db > -9.f ? Colours::accent() : catfx::viz::green()); g.fillRect(x, yDb(db), 18.f, bot - yDb(db));
            g.setColour(catfx::viz::green()); g.setFont(juce::FontOptions(11.f, juce::Font::bold)); g.drawText(c == 0 ? "L" : "R", (int) x, 2, 18, 12, juce::Justification::centred);
        }
        // gain reduction per band: grows downward from the top, 0 .. -24 dB
        const float grTop = top, grH = h; auto yGr = [&](float db) { return grTop + juce::jlimit(0.f, 1.f, db / 24.f) * grH; };
        for (int b = 0; b < 3; ++b)
        {
            const float x = 116.f + (float) b * 26.f, gr = -20.f * std::log10(std::max(1e-4f, proc.engine.mGain[b].load(std::memory_order_relaxed)));
            g.setColour(well()); g.fillRect(x, grTop, 18.f, grH); g.setColour(bandCol(b)); g.fillRect(x, grTop, 18.f, yGr(gr) - grTop);
            g.setFont(juce::FontOptions(11.f, juce::Font::bold)); g.drawText(b == 0 ? "L" : b == 1 ? "M" : "H", (int) x, 2, 18, 12, juce::Justification::centred);
        }
        g.setFont(juce::FontOptions(10.f)); g.setColour(hotRed());
        for (float t : { 0.f, 3.f, 6.f, 12.f, 24.f }) { g.drawText(t == 0.f ? "0" : "-" + juce::String((int) t), 196, (int) yGr(t) - 5, 28, 10, juce::Justification::left); g.setColour(catfx::viz::grid().brighter(0.4f)); g.drawHorizontalLine((int) yGr(t), 112.f, 196.f); g.setColour(hotRed()); }
        g.setColour(Colours::dim()); g.setFont(juce::FontOptions(10.5f));
        g.drawText("L  R  (dB)", 30, (int) bot + 4, 80, 12, juce::Justification::centred); g.drawText("GR: low  mid  high", 106, (int) bot + 4, 120, 12, juce::Justification::centred);
    }
private:
    Scaredy& proc;
};

const char* kLogoSvg = R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 128 128"><rect width="128" height="128" rx="26" fill="#3b3516"/><path d="M24 78 L30 30 L56 52 L72 52 L98 30 L104 78 Q104 106 64 106 Q24 106 24 78 Z" fill="#e6ce55"/><path d="M33 44 L36 66 L48 56 Z M95 44 L92 66 L80 56 Z" fill="#3b3516" opacity=".55"/><circle cx="50" cy="76" r="5" fill="#3b3516"/><circle cx="78" cy="76" r="5" fill="#3b3516"/><path d="M60 86 L68 86 L64 91 Z" fill="#f6e697"/></svg>)SVG";

// the speed read-out: note length when synced, Hz (drag to change) when free
class SpeedBox : public juce::Component, public juce::SettableTooltipClient
{
public:
    SpeedBox(Scaredy& p) : proc(p), hzChip(*p.apvts.getParameter("hz"), "", [](float v) { return juce::String(v, v < 10.f ? 2 : 1) + " Hz"; }, catui::SodaPop::aqua()),
                           aSpeed(*p.apvts.getParameter("speed"), [this](float) { repaint(); })
    { addChildComponent(hzChip); }
    void step(int d) { const int n = (int) Scaredy::speedNames().size(); const int i = (int) std::lround(proc.get("speed")); aSpeed.setValueAsCompleteGesture((float) ((i + d + n) % n)); }
    void resized() override { hzChip.setBounds(getLocalBounds()); }
    void paint(juce::Graphics& g) override
    {
        const bool sync = proc.get("sync") > 0.5f; hzChip.setVisible(! sync);
        auto r = getLocalBounds().toFloat(); g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle(r, 6.f); g.setColour(Colours::dim().withAlpha(0.4f)); g.drawRoundedRectangle(r.reduced(0.5f), 6.f, 1.f);
        if (sync) { g.setColour(catui::SodaPop::aqua()); g.setFont(juce::FontOptions(22.f)); g.drawText(Scaredy::speedNames()[(size_t) juce::jlimit(0, 13, (int) std::lround(proc.get("speed")))], getLocalBounds(), juce::Justification::centred); }
    }
private:
    Scaredy& proc;
public:
    ValueChip hzChip;
private:
    juce::ParameterAttachment aSpeed;
};

struct Content : public juce::Component
{
    Scaredy& proc; ScaredyEditor& ed;
    catui::RenderGate gate { *this };
    std::unique_ptr<juce::Drawable> logo;
    std::vector<std::function<void(bool)>> tipSetters;

    // top
    ToggleChip power;
    struct GearButton : juce::Button
    {
        GearButton() : juce::Button("settings") {}
        void paintButton(juce::Graphics& g, bool hi, bool down) override
        {
            auto r = getLocalBounds().toFloat().reduced(0.5f);
            const juce::Colour amber(0xffFFA600), pan(0xff2C2C62);
            g.setColour(hi || down ? pan.brighter(0.15f) : pan); g.fillEllipse(r);
            g.setColour(amber); g.drawEllipse(r, 1.5f);
            auto c = r.getCentre(); const float R = r.getWidth() * 0.5f;
            juce::Path gear;
            for (int i = 0; i < 8; ++i)
            { gear.addRectangle(-R * 0.10f, -R * 0.56f, R * 0.20f, R * 0.18f); gear.applyTransform(juce::AffineTransform::rotation(juce::MathConstants<float>::pi / 4.f)); }
            g.fillPath(gear, juce::AffineTransform::translation(c.x, c.y));
            g.drawEllipse(c.x - R * 0.34f, c.y - R * 0.34f, R * 0.68f, R * 0.68f, R * 0.16f);
        }
    } gearBtn;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" }, tipsBtn { "i" }, menuBtn { "Menu" }, sizeBtn { "100%" };
    juce::ComboBox presetBox;
    // left
    ToggleChip multi; ChoiceStrip slope; CrossGraph cross; ChoiceStrip linkedStrip; ToggleChip linear, scTrig;
    juce::Slider knob[3]; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> knobAtt[3]; std::unique_ptr<SoloBtn> solo[3];
    // middle
    EnvEditor env; ShapeRow shapes { env, proc }; std::vector<std::unique_ptr<ToolBtn>> tools; CycleChip snapChip, gridChip; ValueChip swingChip;
    PropStrip specMode; SpectrumView spectrum;
    // right
    juce::Slider amount; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAtt; Meters meters;
    // drive and loudness row
    juce::Slider dknob[7]; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> dknobAtt[7];
    // bottom
    SpeedBox speed; juce::TextButton spPrev { "<" }, spNext { ">" }; ChoiceStrip syncStrip; juce::ComboBox trigBox; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> trigAtt;
    ToolBtn lock; ValueChip thresh; ToggleChip limOn; ValueChip ceilChip, smoothChip, offsetChip; ChoiceStrip offUnit;

    static juce::RangedAudioParameter& par(Scaredy& p, const char* id) { return *p.apvts.getParameter(id); }
    void tip(juce::SettableTooltipClient& c, const juce::String& t) { tipSetters.push_back([&c, t](bool on) { c.setTooltip(on ? t : juce::String()); }); }
    void applyTips() { const bool on = proc.tipsOn(); for (auto& f : tipSetters) f(on); }

    Content(ScaredyEditor& e, Scaredy& p)
        : proc(p), ed(e),
          power(par(p, "power"), "POWER", catui::SodaPop::mint(), true),
          multi(par(p, "multiband"), "ON", catui::SodaPop::mint(), true), slope(par(p, "slope"), { "12", "24", "48" }, catui::SodaPop::mint()),
          cross(p), linkedStrip(par(p, "linked"), { "Unlinked", "Linked" }, catui::SodaPop::orchid()),
          linear(par(p, "linear"), "Linear phase", catui::SodaPop::orange()), scTrig(par(p, "sctrig"), "Sidechain trigger", catui::SodaPop::hotPink()),
          env(p), snapChip(par(p, "snap"), [](int i) { return juce::String(i ? "Snap on" : "Snap off"); }, catui::SodaPop::aqua(), 2),
          gridChip(par(p, "grid"), [](int i) { static const char* n[4] = { "1/4", "1/8", "1/16", "1/32" }; return "Grid " + juce::String(n[juce::jlimit(0, 3, i)]); }, catui::SodaPop::aqua(), 4),
          swingChip(par(p, "swing"), "Swing", [](float v) { return juce::String((int) std::lround(v)) + "%"; }, catui::SodaPop::aqua()),
          specMode(p, "specview", { "Spectrum", "Level" }, catui::SodaPop::mint()), spectrum(p), meters(p),
          speed(p), syncStrip(par(p, "sync"), { "Hz", "Sync" }, catui::SodaPop::mint()),
          lock(9, [this] { proc.setProp("locked", ! (bool) proc.getProp("locked", false)); lock.on = (bool) proc.getProp("locked", false); lock.repaint(); env.repaint(); }),
          thresh(par(p, "thresh"), "Thresh", [](float v) { return juce::String(v, 0) + " dB"; }, catui::SodaPop::hotPink(), true),
          limOn(par(p, "limon"), "ON", catui::SodaPop::orange(), true),
          ceilChip(par(p, "limceil"), "", [](float v) { return juce::String(v, 1) + " dB"; }, catui::SodaPop::orange()),
          smoothChip(par(p, "smooth"), "", [](float v) { return juce::String((int) std::lround(v)) + "%"; }, catui::SodaPop::mint()),
          offsetChip(par(p, "offset"), "", [this](float v) { return (v >= 0.f ? "+" : "") + juce::String(v, proc.get("offunit") > 0.5f ? 0 : 1); }, catui::SodaPop::aqua()),
          offUnit(par(p, "offunit"), { "ms", "smp" }, catui::SodaPop::mint())
    {
        logo = juce::Drawable::createFromSVG(*juce::parseXML(juce::String::fromUTF8(kLogoSvg)));
        for (auto* c : std::initializer_list<juce::Component*>{ &power, &prevBtn, &nextBtn, &saveBtn, &tipsBtn, &gearBtn, &menuBtn, &sizeBtn, &presetBox, &multi, &slope, &cross, &linkedStrip, &linear, &scTrig, &env, &shapes,
                                                                &snapChip, &gridChip, &swingChip, &specMode, &spectrum, &amount, &meters, &speed, &spPrev, &spNext, &syncStrip, &trigBox, &lock, &thresh,
                                                                &limOn, &ceilChip, &smoothChip, &offsetChip, &offUnit })
            addAndMakeVisible(*c);
        // knobs
        static const char* ids[3] = { "amtlo", "amtmid", "amthi" };
        for (int b = 0; b < 3; ++b)
        {
            knob[b].setSliderStyle(juce::Slider::RotaryVerticalDrag); knob[b].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
            knob[b].setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            knob[b].setColour(juce::Slider::rotarySliderFillColourId, bandCol(b));
            addAndMakeVisible(knob[b]); knobAtt[b] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, ids[b], knob[b]);
            knob[b].setNumDecimalPlacesToDisplay(0);
            solo[b] = std::make_unique<SoloBtn>(par(p, "solo"), b); addAndMakeVisible(*solo[b]);
        }
        amount.setSliderStyle(juce::Slider::RotaryVerticalDrag); amount.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 24);
        amount.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
        amount.setColour(juce::Slider::rotarySliderFillColourId, catui::SodaPop::orange()); amount.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        amountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, "amount", amount);
        amount.setNumDecimalPlacesToDisplay(0);
        trigBox.addItemList({ "Repeat", "Input hit" }, 1); trigAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.apvts, "trigger", trigBox);
        {   // six drive stages and Loudness
            static const char* dids[7] = { "dtube", "dtape", "dsoft", "ddrive", "dtrans", "dfuzz", "loud" };
            const juce::Colour dcol[7] = { catui::SodaPop::amber(), catui::SodaPop::orange(), catui::SodaPop::mint(), catui::SodaPop::aqua(), catui::SodaPop::orchid(), catui::SodaPop::hotPink(), catui::SodaPop::periwinkle() };
            for (int k = 0; k < 7; ++k)
            {
                auto& kb = dknob[k]; kb.setSliderStyle(juce::Slider::RotaryVerticalDrag); kb.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
                kb.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
                kb.setColour(juce::Slider::rotarySliderFillColourId, dcol[k]); addAndMakeVisible(kb);
                dknobAtt[k] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.apvts, dids[k], kb);
                kb.setNumDecimalPlacesToDisplay(0);
                kb.setDoubleClickReturnValue(true, 0.0);   // double click: back to 0 %
            }
        }
        trigBox.onChange = [this] { thresh.setVisible(trigBox.getSelectedId() == 2 || scTrig.isOn()); };
        for (auto* b : { &spPrev, &spNext }) addAndMakeVisible(*b);
        spPrev.onClick = [this] { speed.step(-1); }; spNext.onClick = [this] { speed.step(1); };
        lock.on = (bool) p.getProp("locked", false);
        // tools
        struct T { int kind; EnvEditor::Tool t; };
        static const T ts[5] = { { 0, EnvEditor::Select }, { 1, EnvEditor::RampUp }, { 2, EnvEditor::RampDown }, { 3, EnvEditor::Hold }, { 4, EnvEditor::Step } };
        for (auto& t : ts)
        {
            auto b = std::make_unique<ToolBtn>(t.kind, [this, tool = t.t] { env.setTool(tool); for (auto& o : tools) if (o->kind() < 5) { o->on = o->kind() == (int) tool; o->repaint(); } });
            b->on = t.t == EnvEditor::Select; tools.push_back(std::move(b));
        }
        tools.push_back(std::make_unique<ToolBtn>(5, [this] { env.deleteSelection(); }));
        tools.push_back(std::make_unique<ToolBtn>(6, [this] { env.selectAll(); }));
        tools.push_back(std::make_unique<ToolBtn>(7, [this] { env.nudge(-1); }));
        tools.push_back(std::make_unique<ToolBtn>(8, [this] { env.nudge(1); }));
        for (auto& t : tools) addAndMakeVisible(*t);
        env.onLaneChanged = [this] { shapes.repaint(); };
        p.onCurvesChanged = [this] { env.notifyExternal(); };
        // presets
        saveBtn.onClick = [this] { askSave(); };
        prevBtn.onClick = [this] { stepPreset(-1); }; nextBtn.onClick = [this] { stepPreset(1); };
        presetBox.onChange = [this] { const int id = presetBox.getSelectedId(); if (id >= 1000) proc.loadUserPreset(userNames[id - 1000]); else if (id >= 1) proc.applyFactoryPreset(id - 1); env.notifyExternal(); };
        rebuildPresets();
        tipsBtn.setClickingTogglesState(true); tipsBtn.setToggleState(p.tipsOn(), juce::dontSendNotification);
        tipsBtn.onClick = [this] { proc.setTipsOn(tipsBtn.getToggleState()); applyTips(); };
        menuBtn.onClick = [this] { showMenu(); };
        gearBtn.onClick = [this] { showSettings(); };
        sizeBtn.onClick = [this] { ed.setSize(kBaseW, kBaseH); };
        trigBox.onChange();
        setTips();
        gate.onFrame = [this] { spectrum.analyse(); env.repaint(); spectrum.repaint(); cross.repaint(); meters.repaint(); repaint(878, 270, 250, 40); repaint(28, 254, 234, 24); };
        gate.onChange = [this](bool active) { proc.viewActive.store(active); };
        gate.start(30);
        proc.viewActive.store(true);
    }
    ~Content() override { gate.stop(); proc.viewActive.store(false); proc.onCurvesChanged = nullptr; }

    juce::StringArray userNames;
    void rebuildPresets()
    {
        presetBox.clear(juce::dontSendNotification);
        for (int i = 0; i < Scaredy::numFactoryPresets(); ++i) presetBox.addItem(Scaredy::factoryPresetName(i), i + 1);
        userNames = proc.userPresetNames();
        if (userNames.size() > 0) { presetBox.addSeparator(); for (int j = 0; j < userNames.size(); ++j) presetBox.addItem(userNames[j], 1000 + j); }
        presetBox.setText(proc.getProp("preset", "Default").toString(), juce::dontSendNotification);
    }
    void stepPreset(int d)
    {
        std::vector<int> ids; for (int i = 0; i < presetBox.getNumItems(); ++i) if (presetBox.getItemId(i) != 0) ids.push_back(presetBox.getItemId(i));
        if (ids.empty()) return;
        int cur = 0; for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == presetBox.getSelectedId()) cur = (int) i;
        presetBox.setSelectedId(ids[(size_t) ((cur + d + (int) ids.size()) % (int) ids.size())], juce::sendNotification);
    }
    void askSave()
    {
        auto* w = new juce::AlertWindow("Save preset", "Name for your preset (saved in the Studio 87 presets folder):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("name", proc.getProp("preset", "My preset").toString());
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Content> self(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create([self, w](int r)
        { if (r == 1 && self != nullptr) { if (self->proc.saveUserPreset(w->getTextEditorContents("name"))) self->rebuildPresets(); } }), true);
    }
    void showSettings()
    {
#ifdef JucePlugin_VersionString
        const juce::String ver = JucePlugin_VersionString;
#else
        const juce::String ver = "?";
#endif
        juce::PopupMenu m;
        m.addItem(100, "Studio 87 Scaredy Cat v" + ver, false);
        m.addSeparator();
        m.addItem(1, "Open GitHub page"); m.addItem(2, "Open releases page");
        m.addItem(3, "Info bubbles", true, proc.tipsOn());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&gearBtn), [this](int r)
        {
            const juce::String url("https://github.com/SpeakingColors/scaredy-cat");
            if (r == 1) juce::URL(url).launchInDefaultBrowser();
            else if (r == 2) juce::URL(url + "/releases").launchInDefaultBrowser();
            else if (r == 3) { proc.setTipsOn(! proc.tipsOn()); tipsBtn.setToggleState(proc.tipsOn(), juce::dontSendNotification); applyTips(); }
        });
    }
    void showMenu()
    {
        juce::PopupMenu m;
        m.addItem(1, "Hover tips", true, proc.tipsOn());
        m.addItem(2, "Safety limiter on the output (never above 0 dBFS)", true, proc.get("safelim") > 0.5f);
        m.addItem(3, "Pause drawing while in the background", true, pauseBg);
        m.addSeparator(); m.addItem(4, "Reset window size (100%)"); m.addItem(5, "Open the presets folder");
        m.addSeparator(); m.addItem(6, "About Scaredy Cat");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuBtn), [this](int r)
        {
            if (r == 1) { proc.setTipsOn(! proc.tipsOn()); tipsBtn.setToggleState(proc.tipsOn(), juce::dontSendNotification); applyTips(); }
            else if (r == 2) { auto* pr = proc.apvts.getParameter("safelim"); pr->beginChangeGesture(); pr->setValueNotifyingHost(proc.get("safelim") > 0.5f ? 0.f : 1.f); pr->endChangeGesture(); }
            else if (r == 3) { pauseBg = ! pauseBg; gate.setPauseInBackground(pauseBg); }
            else if (r == 4) ed.setSize(kBaseW, kBaseH);
            else if (r == 5) { auto d = proc.presetFolder(); d.createDirectory(); d.revealToUser(); }
            else if (r == 6) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "Scaredy Cat",
                "Scaredy Cat by Studio 87.\nFree and open source (AGPL-3.0-or-later).\nIt collects no data and never touches the network.\nhttps://github.com/SpeakingColors/scaredy-cat");
        });
    }
    bool pauseBg = true;

    void setTips()
    {
        tip(power, "Power. Off is an exact bypass: the sound passes untouched.");
        tip(prevBtn, "Previous preset."); tip(nextBtn, "Next preset."); tip(presetBox, "Factory presets, then your own (Save).");
        tip(saveBtn, "Save the current settings and the three envelopes as your own preset."); tip(tipsBtn, "Turn these hover tips on or off."); tip(menuBtn, "More options."); tip(gearBtn, "Settings: version, link to the GitHub page for new versions"); tip(sizeBtn, "Back to the normal window size (100 %). You can also drag any edge or corner of the window.");
        tip(multi, "Multiband on: three bands with their own envelopes. Off: one envelope for everything (the middle lane).");
        tip(slope, "How steep the split between the bands is, in dB per octave."); tip(cross, "The three bands. Drag the white lines (arrows) to move the crossovers, double click a line to reset it.");
        for (int b = 0; b < 3; ++b) { tip(knob[b], juce::String("How much of the ") + (b == 0 ? "low" : b == 1 ? "mid" : "high") + " band envelope is applied. 0 % leaves that band alone."); tip(*solo[b], "Solo: listen to this band only."); }
        tip(linkedStrip, "Linked: all three bands use the same envelope, edit one and all follow."); tip(linear, "Linear phase split: no phase shift between the bands. Adds latency (your host compensates) and a softer low crossover.");
        { static const char* dt[7] = { "Tube drive: warm, even harmonics. Mix it with the other stages, they run in series. Double click: 0 %.", "Tape drive: gentle, soft top end. Double click: 0 %.", "Soft drive: smooth and round. Double click: 0 %.",
              "PurrOne Drive: a tanh push. Above 80 % it gets nasty. Double click: 0 %.", "PurrOne Transistor: lopsided, adds even harmonics. Above 80 % it gets nasty. Double click: 0 %.", "PurrOne Fuzz: biased clip with an octave-up growl. Above 80 % it gets nasty. Double click: 0 %.",
              "Loudness: a smooth opto leveller (LA-2A style) and a hidden soft clipper before the limiter. 0 % is off. The knob is the dry/wet. Double click: 0 %." };
          for (int k = 0; k < 7; ++k) tip(dknob[k], dt[k]); }
        tip(scTrig, "Hits on the sidechain input restart the envelope (needs a sidechain connected).");
        tip(env, "Draw the volume of each band over one cycle. Drag points, double click to add one, right click a point to delete it. Drag the line between two points up or down to bend it.");
        tip(shapes, "Quick shapes for the selected lane. Shapes opens the library (built in and your own), + saves the lane as your own shape, - deletes one of yours.");
        tip(snapChip, "Points snap to the grid while you drag (hold Ctrl to move freely). Click to switch."); tip(gridChip, "Grid size for editing. Click to change.");
        tip(swingChip, "Delays the second half of each cycle for a swung feel. Drag up or down.");
        const char* tn[9] = { "Select and move points.", "Draw a ramp up: drag across the lane.", "Draw a ramp down: drag across the lane.", "Draw a flat hold: drag across the lane.", "Draw a step: hold the level, then jump.", "Delete the selected points (nothing selected: reset the lane).", "Select all points in the lane.", "Move the selection (or the whole lane) one grid step left.", "Move the selection (or the whole lane) one grid step right." };
        for (size_t i = 0; i < tools.size() && i < 9; ++i) tip(*tools[i], tn[i]);
        tip(specMode, "Spectrum: the original sound by frequency (dashed) and what the ducking takes away from it (filled). Level: how many dB each band loses."); tip(spectrum, "Drag the arrows to move the crossovers. Double click a line to reset it.");
        tip(amount, "AMOUNT: how deep the envelopes cut. 0 % is an exact bypass, 100 % is the full depth you drew."); tip(meters, "Output peak meters (L, R) and the gain reduction of each band, in dB.");
        tip(speed, "How fast the envelope repeats: a note length in Sync mode, or Hz."); tip(spPrev, "Slower."); tip(spNext, "Faster."); tip(syncStrip, "Sync follows the host tempo. Hz runs at a fixed speed.");
        tip(trigBox, "Repeat: loops with the tempo. Input hit: every loud hit restarts the envelope (no tempo needed)."); tip(lock, "Lock the envelopes so a stray click cannot change them.");
        tip(thresh, "How loud a hit must be to restart the envelope. Drag up or down.");
        tip(limOn, "Lookahead limiter after the ducking."); tip(ceilChip, "Highest level the limiter lets through. Drag up or down.");
        tip(smoothChip, "Rounds the envelope edges so steps do not click. Drag up or down."); tip(offsetChip, "Moves the envelope later (+) or earlier (-) so the pump lines up with your kick."); tip(offUnit, "Offset in milliseconds or samples.");
        applyTips();
    }

    void resized() override
    {
        power.setBounds(342, 12, 62, 30); prevBtn.setBounds(414, 12, 28, 30); presetBox.setBounds(446, 12, 250, 30); nextBtn.setBounds(700, 12, 28, 30); saveBtn.setBounds(740, 12, 56, 30);
        sizeBtn.setBounds(930, 12, 56, 30); tipsBtn.setBounds(992, 12, 34, 30); menuBtn.setBounds(1032, 12, 60, 30); gearBtn.setBounds(1098, 12, 30, 30);
        // left panel
        multi.setBounds(120, 70, 44, 24); slope.setBounds(82, 104, 124, 26); cross.setBounds(28, 138, 234, 116);
        for (int b = 0; b < 3; ++b) { const int x = 28 + b * 81; knob[b].setBounds(x, 316, 72, 84); solo[b]->setBounds(x + 24, 402, 24, 20); }
        linkedStrip.setBounds(28, 432, 234, 26); linear.setBounds(30, 470, 220, 24);
        // middle
        for (size_t i = 0; i < tools.size(); ++i) { const int x = i < 5 ? 292 + (int) i * 34 : 468 + ((int) i - 5) * 34; tools[i]->setBounds(x, 66, 30, 30); }
        snapChip.setBounds(612, 66, 60, 30); gridChip.setBounds(674, 66, 72, 30); swingChip.setBounds(748, 66, 104, 30);
        env.setBounds(292, 104, 560, 330); shapes.setBounds(292, 442, 560, 34);
        specMode.setBounds(722, 486, 130, 22); spectrum.setBounds(292, 512, 560, 128);
        // right
        amount.setBounds(900, 96, 190, 196); meters.setBounds(880, 312, 232, 322);
        // drive and loudness row
        for (int k = 0; k < 6; ++k) dknob[k].setBounds(40 + k * 92, 686, 84, 72);
        dknob[6].setBounds(706, 680, 110, 80);
        // bottom
        const int by = 120;
        speed.setBounds(102, 664 + by, 84, 46); spPrev.setBounds(74, 670 + by, 26, 34); spNext.setBounds(190, 670 + by, 26, 34); syncStrip.setBounds(222, 674 + by, 68, 26);
        trigBox.setBounds(352, 672 + by, 90, 30); lock.setBounds(446, 672 + by, 30, 30); thresh.setBounds(480, 672 + by, 82, 30);
        scTrig.setBounds(582, 674 + by, 146, 26);
        limOn.setBounds(784, 672 + by, 32, 30); ceilChip.setBounds(818, 670 + by, 58, 34);
        smoothChip.setBounds(926, 670 + by, 34, 34); offsetChip.setBounds(1008, 670 + by, 42, 34); offUnit.setBounds(1054, 674 + by, 60, 26);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(Colours::panel());
        if (logo) logo->drawWithin(g, juce::Rectangle<float>(10.f, 4.f, 46.f, 46.f), juce::RectanglePlacement::centred, 1.f);
        g.setColour(Colours::accent()); g.setFont(juce::FontOptions(22.f, juce::Font::bold)); g.drawText("SCAREDY CAT", 64, 5, 250, 26, juce::Justification::left);
        g.setColour(Colours::dim()); g.setFont(juce::FontOptions(11.5f)); g.drawText("ducks when the kick hits", 64, 30, 250, 14, juce::Justification::left);
        auto panel = [&](juce::Rectangle<int> r) { g.setColour(card()); g.fillRoundedRectangle(r.toFloat(), 8.f); };
        panel({ 14, 62, 262, 580 }); panel({ 868, 62, 254, 580 }); panel({ 14, 648, 1108, 112 }); panel({ 14, 768, 1108, 76 });
        g.setColour(well()); g.fillRoundedRectangle(24.f, 520.f, 242.f, 112.f, 7.f);
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.setColour(title()); g.drawText("MULTIBAND", 28, 70, 90, 24, juce::Justification::left); g.drawText("BAND AMOUNTS", 28, 282, 234, 16, juce::Justification::centred);
        g.drawText("AMOUNT", 868, 70, 254, 18, juce::Justification::centred); g.drawText("WHAT YOU SEE", 34, 526, 160, 16, juce::Justification::left);
        g.setColour(Colours::text()); g.setFont(juce::FontOptions(12.f)); g.drawText("Slope", 28, 104, 50, 26, juce::Justification::left);
        g.setColour(Colours::dim()); g.setFont(juce::FontOptions(11.5f));
        g.drawFittedText("Dashed: the original sound.\nFill: what the ducking removes.\nLevel view: dB lost per band.\nThe sidechain only triggers.", 34, 548, 226, 78, juce::Justification::topLeft, 6);
        g.drawText("Envelopes", 28, 296, 1, 1, juce::Justification::left);
        {   // the two crossover frequencies, as two small value chips under the small graph
            auto hz = [](float f) { return f >= 1000.f ? juce::String(juce::String(f / 1000.f, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".")) + " kHz" : juce::String((int) std::lround(f)) + " Hz"; };
            auto chip = [&](juce::Rectangle<float> r, const juce::String& name, const juce::String& val)
            {
                g.setColour(juce::Colour(0xff0b0b20)); g.fillRoundedRectangle(r, 6.f);
                g.setColour(juce::Colour(0xff3a3a6a)); g.drawRoundedRectangle(r, 6.f, 1.f);
                g.setColour(juce::Colour(0xff9aa0c8)); g.setFont(juce::FontOptions(10.5f)); g.drawText(name, r.reduced(6.f, 0.f).toNearestInt(), juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff00e5c0)); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(val, r.reduced(6.f, 0.f).toNearestInt(), juce::Justification::centredRight);
            };
            chip({ 28.f, 257.f, 114.f, 20.f }, "Low | Mid", hz(proc.get("xlo")));
            chip({ 148.f, 257.f, 114.f, 20.f }, "Mid | High", hz(proc.get("xhi")));
        }
        g.setColour(title()); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText("BAND SPECTRUM", 292, 488, 200, 18, juce::Justification::left);
        g.setColour(Colours::dim()); g.setFont(juce::FontOptions(10.5f)); g.drawText("dashed: original   filled: removed", 500, 488, 220, 18, juce::Justification::right);
        for (int b = 0; b < 3; ++b)
        {
            const int x = 28 + b * 81; g.setColour(juce::Colour(0xff111130)); g.fillRoundedRectangle((float) x, 298.f, 72.f, 126.f, 7.f);
            g.setColour(bandCol(b)); g.setFont(juce::FontOptions(12.f, juce::Font::bold)); g.drawText(b == 0 ? "Low" : b == 1 ? "Mid" : "High", x, 300, 72, 16, juce::Justification::centred);
        }
        // readouts
        const float outPk = std::max(proc.peakL.load(), proc.peakR.load());
        float gr = 0.f; for (int b = 0; b < 3; ++b) gr = std::max(gr, -20.f * std::log10(std::max(1e-4f, proc.engine.mGain[b].load())));
        g.setFont(juce::FontOptions(13.f, juce::Font::bold));
        g.setColour(Colours::dim()); g.drawText("OUT", 900, 288, 34, 18, juce::Justification::left); g.setColour(catui::SodaPop::mint()); g.drawText(dbText(outPk), 934, 288, 56, 18, juce::Justification::left);
        g.setColour(Colours::dim()); g.drawText("GR", 1000, 288, 24, 18, juce::Justification::left); g.setColour(hotRed()); g.drawText("-" + juce::String(gr, 1), 1024, 288, 56, 18, juce::Justification::left);
        // bottom labels and group frames
        g.setFont(juce::FontOptions(11.f, juce::Font::bold)); g.setColour(Colours::dim());
        g.drawText("SPEED", 26, 798, 48, 18, juce::Justification::left); g.drawText("TRIGGER", 298, 798, 54, 18, juce::Justification::left);
        {   // drive row
            static const char* dn[7] = { "TUBE", "TAPE", "SOFT", "DRIVE", "TRANS", "FUZZ", "LOUDNESS" };
            const juce::Colour dcol[7] = { catui::SodaPop::amber(), catui::SodaPop::orange(), catui::SodaPop::mint(), catui::SodaPop::aqua(), catui::SodaPop::orchid(), catui::SodaPop::hotPink(), catui::SodaPop::periwinkle() };
            g.setColour(title()); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText("DRIVE: six stages in series, mix any of them", 26, 652, 560, 18, juce::Justification::left); g.drawText("LOUDNESS", 706, 652, 110, 18, juce::Justification::centred);
            g.setColour(Colours::dim().withAlpha(0.5f)); g.drawVerticalLine(660, 656.f, 752.f); g.drawVerticalLine(836, 656.f, 752.f);
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            for (int k = 0; k < 6; ++k) { g.setColour(dcol[k]); g.drawText(dn[k], 40 + k * 92, 670, 84, 14, juce::Justification::centred); }
            g.setColour(Colours::dim()); g.setFont(juce::FontOptions(11.5f));
            g.drawFittedText("Order: Soft, Tape, Tube, Drive, Transistor, Fuzz. Each stage keeps the level about the same. Drive, Transistor and Fuzz are the PurrOne stages and get nasty above 80 %.\n\nLoudness: a smooth opto leveller plus a hidden soft clipper before the limiter. 0 % is off.", 848, 656, 264, 96, juce::Justification::topLeft, 8);
        }
        auto frame = [&](juce::Rectangle<int> r, juce::Colour c, const juce::String& t, int tw)
        { g.setColour(c.withAlpha(0.8f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 7.f, 1.4f); g.setColour(Colours::dim()); if (tw > 0) g.drawText(t, r.getX() + 8, r.getY() + 14, tw, 18, juce::Justification::left); };
        frame({ 572, 782, 160, 50 }, catui::SodaPop::hotPink(), "", 0); frame({ 738, 782, 140, 50 }, catui::SodaPop::orange(), "LIMIT", 40);
        frame({ 884, 782, 78, 50 }, catui::SodaPop::mint(), "SMTH", 34); frame({ 968, 782, 154, 50 }, catui::SodaPop::aqua(), "OFFSET", 44);
    }
};
}   // namespace

struct ScaredyEditor::Impl
{
    ScaredyLAF laf; Content content; juce::TooltipWindow tips; std::unique_ptr<juce::ResizableBorderComponent> border;
    Impl(ScaredyEditor& e, Scaredy& p) : content(e, p), tips(&e, 1000) {}
};

ScaredyEditor::ScaredyEditor(Scaredy& p) : AudioProcessorEditor(&p)
{
    impl = std::make_unique<Impl>(*this, p);
    setLookAndFeel(&impl->laf);
    addAndMakeVisible(impl->content);
    sendLookAndFeelChange();
    setResizable(true, false);   // every edge and corner (the border below), fixed aspect, 60 % to 200 %
    setResizeLimits(kMinW, (int) std::lround((double) kMinW * kBaseH / kBaseW), kMaxW, (int) std::lround((double) kMaxW * kBaseH / kBaseW));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) kBaseW / (double) kBaseH);
    impl->border = std::make_unique<juce::ResizableBorderComponent>(this, getConstrainer());
    impl->border->setBorderThickness(juce::BorderSize<int>(6));
    addAndMakeVisible(*impl->border);
    const int w = juce::jlimit(kMinW, kMaxW, (int) p.getProp("uiw", kBaseW));
    setSize(w, (int) std::lround((double) w * kBaseH / kBaseW));
}
ScaredyEditor::~ScaredyEditor() { setLookAndFeel(nullptr); }
void ScaredyEditor::paint(juce::Graphics& g)
{
    g.fillAll(Colours::panel());
    catui::drawWoodCheeks(g, getLocalBounds(), (int) std::lround(kCheek * (double) getWidth() / kBaseW));
}
void ScaredyEditor::resized()
{
    if (impl == nullptr) return;
    const float s = (float) getWidth() / (float) kBaseW;
    impl->content.setBounds(0, 0, kContentW, kBaseH);
    if (impl->border != nullptr) { impl->border->setBounds(getLocalBounds()); impl->border->toFront(false); }
    impl->content.setTransform(juce::AffineTransform::scale(s).translated((float) kCheek * s, 0.f));
    if (auto* pr = dynamic_cast<Scaredy*>(&processor)) pr->setProp("uiw", getWidth());
}
