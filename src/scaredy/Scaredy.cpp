#include "Scaredy.h"
#include "ScaredyEditor.h"

using namespace scaredy;

namespace
{
const char* kTipSpeed = "How fast the envelope repeats. In Sync mode it is a note length of the host tempo (1/4 = one cycle per beat). In Hz mode it runs free.";
std::vector<catfx::KnobSpec> makeKnobSpecs()
{
    return {
        { "amount", "Amount", " %", 0.f, 100.f, 100.f, 0.f, "How deep the envelopes cut. 0 % is an exact bypass (the sound passes untouched), 100 % is the full depth you drew." },
        { "amtlo", "Low", " %", 0.f, 100.f, 100.f, 0.f, "How much of the low band envelope is applied (0 % leaves the low band alone)." },
        { "amtmid", "Mid", " %", 0.f, 100.f, 100.f, 0.f, "How much of the mid band envelope is applied." },
        { "amthi", "High", " %", 0.f, 100.f, 100.f, 0.f, "How much of the high band envelope is applied." },
        { "xlo", "Low crossover", " Hz", 40.f, 400.f, 120.f, 120.f, "Where the low band ends. Drag the line in the crossover graph or the spectrum." },
        { "xhi", "High crossover", " Hz", 800.f, 8000.f, 2400.f, 2400.f, "Where the high band starts." },
        { "hz", "Speed (Hz)", " Hz", 0.05f, 30.f, 2.f, 2.f, "Speed in cycles per second when Sync is off." },
        { "swing", "Swing", " %", 0.f, 100.f, 0.f, 0.f, "Delays the second half of every cycle for a swung feel." },
        { "smooth", "Smooth", " %", 0.f, 100.f, 4.f, 0.f, "Rounds the envelope edges so sharp steps do not click. Higher is softer." },
        { "dtube", "Tube", " %", 0.f, 100.f, 0.f, 0.f, "Tube drive: warm, even harmonics. Mix it with the other five. The level stays about the same." },
        { "dtape", "Tape", " %", 0.f, 100.f, 0.f, 0.f, "Tape drive: gentle, soft top end." },
        { "dsoft", "Soft", " %", 0.f, 100.f, 0.f, 0.f, "Soft drive: smooth and round." },
        { "ddrive", "Drive", " %", 0.f, 100.f, 0.f, 0.f, "PurrOne Drive: a tanh push, gets nasty above 80 %." },
        { "dtrans", "Transistor", " %", 0.f, 100.f, 0.f, 0.f, "PurrOne Transistor: lopsided, adds even harmonics, gets nasty above 80 %." },
        { "dfuzz", "Fuzz", " %", 0.f, 100.f, 0.f, 0.f, "PurrOne Fuzz: biased clip with octave-up growl, gets nasty above 80 %." },
        { "loud", "Loudness", " %", 0.f, 100.f, 0.f, 0.f, "Makes it louder without pumping: a smooth opto leveller (LA-2A style) and a hidden soft clipper before the limiter. 0 % is off, the knob is the dry/wet." },
        { "limceil", "Limit ceiling", " dB", -12.f, 0.f, -0.3f, 0.f, "Highest level the limiter lets through." },
        { "offset", "Offset", "", -100.f, 100.f, 0.f, 0.f, "Moves the envelope later (positive) or earlier (negative), in ms or samples. Use it to line the pump up with the kick." },
        { "thresh", "Hit threshold", " dB", -60.f, 0.f, -24.f, 0.f, "How loud a hit must be to restart the envelope in the hit trigger modes." },
    };
}
std::vector<catfx::ChoiceSpec> makeChoiceSpecs()
{
    return {
        { "power", "Power", { "Off", "On" }, 1, "Turns the effect on or off. Off is an exact bypass." },
        { "speed", "Speed", Scaredy::speedNames().empty() ? juce::StringArray() : juce::StringArray(Scaredy::speedNames().data(), (int) Scaredy::speedNames().size()), 3, kTipSpeed },
        { "sync", "Sync", { "Hz", "Sync" }, 1, "Sync follows the host tempo. Hz runs at a fixed speed." },
        { "trigger", "Trigger", { "Repeat", "Input hit" }, 0, "Repeat: the envelope loops with the tempo. Input hit: every loud hit in the input restarts it (no tempo needed)." },
        { "slope", "Slope", { "12", "24", "48" }, 1, "Steepness of the band split in dB per octave." },
        { "multiband", "Multiband", { "Off", "On" }, 1, "On: three bands with their own envelopes. Off: one envelope for the whole sound." },
        { "linear", "Linear phase", { "Off", "On" }, 0, "Splits the bands without phase shift. Adds latency (reported to your host) and a softer low crossover." },
        { "limon", "Limiter", { "Off", "On" }, 1, "Lookahead limiter after the ducking." },
        { "offunit", "Offset unit", { "MS", "SAMPS" }, 0, "Offset in milliseconds or in samples." },
        { "linked", "Envelopes", { "Unlinked", "Linked" }, 0, "Linked: all three bands use the same envelope (edit one, all follow)." },
        { "solo", "Solo", { "Off", "Low", "Mid", "High" }, 0, "Listen to one band only." },
        { "sctrig", "Sidechain triggers", { "Off", "On" }, 0, "The sidechain input (if connected) restarts the envelope on every hit." },
        { "grid", "Grid", { "1/4", "1/8", "1/16", "1/32" }, 2, "Grid for editing the envelope." },
        { "snap", "Snap", { "Off", "On" }, 1, "Points snap to the grid while you drag." },
    };
}
Curve defaultCurve(int lane) { static const float lo[3] = { 0.f, 0.1f, 0.45f }; return shapeCurve(0, lo[lane]); }
}

const std::vector<juce::String>& Scaredy::speedNames()
{
    static const std::vector<juce::String> n { "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/2T", "1/4T", "1/8T", "1/16T", "1/4.", "1/8.", "1/16." };
    return n;
}
double Scaredy::speedBeats(int i)
{
    static const double b[14] = { 8, 4, 2, 1, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.5, 0.75, 0.375 };
    return b[juce::jlimit(0, 13, i)];
}

Scaredy::Scaredy() : catfx::FxBase("Scaredy Cat", makeKnobSpecs(), makeChoiceSpecs(), true)
{
    auto P = [this](const char* id) { return apvts.getRawParameterValue(id); };
    pAmount = P("amount"); pLo = P("amtlo"); pMid = P("amtmid"); pHi = P("amthi"); pXlo = P("xlo"); pXhi = P("xhi"); pHz = P("hz"); pSwing = P("swing");
    pSmooth = P("smooth"); pDTube = P("dtube"); pDTape = P("dtape"); pDSoft = P("dsoft"); pDDrive = P("ddrive"); pDTrans = P("dtrans"); pDFuzz = P("dfuzz"); pLoud = P("loud"); pCeil = P("limceil"); pOffset = P("offset"); pThresh = P("thresh"); pSpeed = P("speed"); pSync = P("sync");
    pTrig = P("trigger"); pSlope = P("slope"); pMulti = P("multiband"); pLinear = P("linear"); pLimon = P("limon");
    pOffunit = P("offunit"); pLinked = P("linked"); pSolo = P("solo"); pSctrig = P("sctrig"); pPower = P("power");
    for (auto& r : ring) for (auto& v : r) v.store(0.f, std::memory_order_relaxed);
    for (int i = 0; i < 3; ++i) curves[(size_t) i] = defaultCurve(i);
    for (int i = 0; i < 3; ++i) bakeCurve(curves[(size_t) i], master + i * (kTable + 1));
    for (auto& b : bufs) std::memcpy(b, master, sizeof master);
    apvts.state.addListener(this);
}
Scaredy::~Scaredy() { apvts.state.removeListener(this); cancelPendingUpdate(); }

juce::AudioProcessorEditor* Scaredy::createEditor() { return new ScaredyEditor(*this); }

Params Scaredy::readParams() const
{
    Params p;
    p.amount = pPower->load() > 0.5f ? (double) pAmount->load() : 0.0;
    p.amtLo = pLo->load(); p.amtMid = pMid->load(); p.amtHi = pHi->load();
    p.xLo = pXlo->load(); p.xHi = pXhi->load();
    p.slope = juce::jlimit(0, 2, (int) std::lround(pSlope->load()));
    p.multiband = pMulti->load() > 0.5f; p.linear = pLinear->load() > 0.5f; p.linked = pLinked->load() > 0.5f;
    p.sync = pSync->load() > 0.5f; p.cycleBeats = speedBeats((int) std::lround(pSpeed->load())); p.hz = pHz->load();
    p.trigger = pTrig->load() > 0.5f ? 1 : 0; p.scTrigger = pSctrig->load() > 0.5f && hasSidechain; p.threshDb = pThresh->load();
    p.swing = pSwing->load(); p.smooth = pSmooth->load();
    p.offset = pOffset->load(); p.offsetSamples = pOffunit->load() > 0.5f;
    p.dTube = pDTube->load(); p.dTape = pDTape->load(); p.dSoft = pDSoft->load(); p.dDrive = pDDrive->load(); p.dTrans = pDTrans->load(); p.dFuzz = pDFuzz->load(); p.loud = pLoud->load();
    p.limOn = pLimon->load() > 0.5f; p.ceilDb = pCeil->load(); p.solo = juce::jlimit(0, 3, (int) std::lround(pSolo->load()));
    return p;
}

void Scaredy::prepare(double sr, int)
{
    engine.prepare(sr);
    wIdx = 0; rIdx = 1; mid.store(2);
    publish();
    const int lat = engine.latency(readParams());
    reportedLatency.store(lat);
    setLatencySamples(lat);
}
void Scaredy::handleAsyncUpdate() { setLatencySamples(reportedLatency.load()); }

void Scaredy::process(juce::AudioBuffer<float>& main, const juce::AudioBuffer<float>* sc)
{
    const int n = main.getNumSamples();
    if (main.getNumChannels() < 2 || n <= 0) return;
    if (mid.load(std::memory_order_acquire) & 4) rIdx = mid.exchange(rIdx, std::memory_order_acq_rel) & 3;   // newest tables, no waiting
    const Params p = readParams();
    const int lat = engine.latency(p);
    if (lat != reportedLatency.load()) { reportedLatency.store(lat); triggerAsyncUpdate(); }
    Timing tm;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tm.playing = pos->getIsPlaying();
            if (auto b = pos->getBpm()) tm.bpm = *b;
            if (auto q = pos->getPpqPosition()) { tm.ppq = *q; tm.havePpq = true; }
        }
    float* L = main.getWritePointer(0); float* R = main.getWritePointer(1);
    const float* sl = nullptr; const float* sr = nullptr;
    if (sc != nullptr && sc->getNumChannels() > 0 && sc->getNumSamples() >= n) { sl = sc->getReadPointer(0); sr = sc->getNumChannels() > 1 ? sc->getReadPointer(1) : sl; }
    const bool view = viewActive.load(std::memory_order_relaxed);
    float pkL = 0.f, pkR = 0.f; unsigned w = wpos.load(std::memory_order_relaxed);
    const double ppqStep = tm.bpm / 60.0 / fs;
    for (int i = 0; i < n; i += 512)
    {
        const int m = std::min(512, n - i);
        if (view)
            for (int k = 0; k < m; ++k)
            {
                ring[0][(w + (unsigned) k) & (kRing - 1)].store(0.5f * (L[i + k] + R[i + k]), std::memory_order_relaxed);
                ring[1][(w + (unsigned) k) & (kRing - 1)].store(sl != nullptr ? 0.5f * (sl[i + k] + sr[i + k]) : 0.f, std::memory_order_relaxed);
            }
        Timing t2 = tm; t2.ppq = tm.ppq + (double) i * ppqStep;
        engine.process(L + i, R + i, sl != nullptr ? sl + i : nullptr, sr != nullptr ? sr + i : nullptr, m, p, t2, bufs[rIdx]);
        for (int k = 0; k < m; ++k)
        {
            pkL = std::max(pkL, std::abs(L[i + k])); pkR = std::max(pkR, std::abs(R[i + k]));
            if (view) ring[2][(w + (unsigned) k) & (kRing - 1)].store(0.5f * (L[i + k] + R[i + k]), std::memory_order_relaxed);
        }
        w += (unsigned) m;
    }
    if (view) wpos.store(w, std::memory_order_release);
    const float decay = (float) std::pow(10.0, -(double) n / std::max(1.0, fs));
    peakL.store(std::max(pkL, peakL.load() * decay), std::memory_order_relaxed);
    peakR.store(std::max(pkR, peakR.load() * decay), std::memory_order_relaxed);
}

// ---------- envelopes ----------
void Scaredy::publish()
{
    std::memcpy(bufs[wIdx], master, sizeof master);
    const int old = mid.exchange(wIdx | 4, std::memory_order_acq_rel);
    wIdx = old & 3;
}
void Scaredy::setCurve(int lane, Curve c, bool alsoOthers)
{
    sanitize(c);
    ignoreState = true;
    for (int i = 0; i < 3; ++i)
        if (i == lane || alsoOthers)
        {
            curves[(size_t) i] = c;
            bakeCurve(c, master + i * (kTable + 1));
            setProp("env" + juce::String(i), juce::String(curveToString(c)));
        }
    ignoreState = false;
    publish();
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));
    if (onCurvesChanged) onCurvesChanged();
}
void Scaredy::loadCurvesFromState()
{
    for (int i = 0; i < 3; ++i)
    {
        const auto s = getProp("env" + juce::String(i), juce::String()).toString();
        curves[(size_t) i] = s.isEmpty() ? defaultCurve(i) : curveFromString(s.toStdString());
        bakeCurve(curves[(size_t) i], master + i * (kTable + 1));
    }
    publish();
    if (onCurvesChanged) onCurvesChanged();
}
void Scaredy::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier& id)
{
    if (ignoreState) return;
    if (id.toString().startsWith("env")) loadCurvesFromState();
}

// ---------- state migration (older versions had one Drive knob with a type, and a spectrum display choice) ----------
namespace
{
void migrateXml(juce::XmlElement& x)
{
    juce::XmlElement* drive = nullptr; juce::XmlElement* type = nullptr; juce::XmlElement* disp = nullptr; bool hasNew = false;
    for (auto* c : x.getChildIterator())
    {
        const auto id = c->getStringAttribute("id");
        if (id == "drive") drive = c; else if (id == "drivetype") type = c; else if (id == "scdisp") disp = c;
        else if (id == "dtube" || id == "dtape" || id == "dsoft") hasNew = true;
    }
    if (drive != nullptr && ! hasNew)
    {
        const double v = drive->getDoubleAttribute("value", 0.0); const int t = type != nullptr ? juce::jlimit(0, 2, (int) std::lround(type->getDoubleAttribute("value", 0.0))) : 0;
        if (v > 0.0) { auto* n = x.createNewChildElement("PARAM"); n->setAttribute("id", t == 0 ? "dtube" : t == 1 ? "dtape" : "dsoft"); n->setAttribute("value", v); }
    }
    for (auto* c : { drive, type, disp }) if (c != nullptr) x.removeChildElement(c, true);
}
}
void Scaredy::setStateInformation(const void* d, int size)
{
    if (auto xml = getXmlFromBinary(d, size))
    {
        migrateXml(*xml);
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        loadCurvesFromState();
    }
}

// ---------- presets ----------
namespace
{
struct FP { const char* name; std::vector<std::pair<const char*, float>> v; int shape[3]; float lo[3]; };
const std::vector<FP>& fps()
{
    static const std::vector<FP> f {
        { "Default", {}, { 0, 0, 0 }, { 0.f, 0.1f, 0.45f } },
        { "Hardstyle kick pump", { { "amount", 100 }, { "speed", 3 }, { "slope", 2 }, { "smooth", 3 }, { "dtube", 12 } }, { 0, 0, 0 }, { 0.f, 0.05f, 0.4f } },
        { "Purr kick pump", { { "amount", 100 }, { "speed", 3 }, { "slope", 2 }, { "smooth", 3 }, { "ddrive", 25 }, { "dtrans", 18 }, { "loud", 25 } }, { 0, 0, 0 }, { 0.f, 0.05f, 0.4f } },
        { "Nasty chop", { { "amount", 100 }, { "speed", 4 }, { "smooth", 4 }, { "dfuzz", 45 }, { "loud", 35 } }, { 1, 1, 1 }, { 0.f, 0.f, 0.15f } },
        { "Sidechain house", { { "amount", 85 }, { "speed", 3 }, { "slope", 1 }, { "smooth", 6 } }, { 0, 0, 0 }, { 0.f, 0.15f, 0.5f } },
        { "Trance gate 1/16", { { "amount", 100 }, { "speed", 3 }, { "smooth", 8 } }, { 8, 8, 8 }, { 0.f, 0.f, 0.1f } },
        { "Eighth-note pump", { { "amount", 90 }, { "speed", 3 }, { "smooth", 5 } }, { 9, 9, 9 }, { 0.f, 0.1f, 0.45f } },
        { "UK hardcore chop", { { "amount", 100 }, { "speed", 4 }, { "swing", 20 }, { "dtape", 25 }, { "smooth", 5 } }, { 1, 1, 1 }, { 0.f, 0.f, 0.2f } },
        { "Slow tremolo", { { "amount", 60 }, { "sync", 0 }, { "hz", 3.5f }, { "smooth", 20 } }, { 11, 11, 11 }, { 0.3f, 0.3f, 0.3f } },
        { "Gentle glue", { { "amount", 35 }, { "speed", 3 }, { "smooth", 15 } }, { 0, 0, 0 }, { 0.2f, 0.3f, 0.5f } },
        { "Half-time swell", { { "amount", 100 }, { "speed", 1 }, { "smooth", 20 } }, { 2, 2, 2 }, { 0.f, 0.f, 0.2f } },
        { "Audio hit duck", { { "amount", 100 }, { "trigger", 1 }, { "sync", 0 }, { "hz", 3.f }, { "thresh", -24 }, { "smooth", 5 } }, { 0, 0, 0 }, { 0.f, 0.1f, 0.4f } },
        { "Bass-only duck", { { "amount", 100 }, { "amtmid", 20 }, { "amthi", 0 }, { "speed", 3 }, { "smooth", 4 } }, { 0, 0, 0 }, { 0.f, 0.f, 0.f } },
    };
    return f;
}
}
int Scaredy::numFactoryPresets() { return (int) fps().size(); }
juce::String Scaredy::factoryPresetName(int i) { return fps()[(size_t) juce::jlimit(0, numFactoryPresets() - 1, i)].name; }
void Scaredy::applyFactoryPreset(int i)
{
    if (i < 0 || i >= numFactoryPresets()) return;
    const auto& f = fps()[(size_t) i];
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p)) r->setValueNotifyingHost(r->getDefaultValue());
    for (auto& kv : f.v)
        if (auto* r = apvts.getParameter(kv.first)) r->setValueNotifyingHost(r->convertTo0to1(kv.second));
    for (int l = 0; l < 3; ++l) setCurve(l, shapeCurve(f.shape[l], f.lo[l]));
    presetIndex = i; setProp("preset", factoryPresetName(i));
}
juce::File Scaredy::presetFolder() const
{
    auto d = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    d = d.getChildFile("Application Support");
   #endif
    return d.getChildFile("Studio 87").getChildFile("ScaredyCat").getChildFile("presets");
}
juce::StringArray Scaredy::userPresetNames() const
{
    juce::StringArray out;
    for (auto& f : presetFolder().findChildFiles(juce::File::findFiles, false, "*.xml")) out.add(f.getFileNameWithoutExtension());
    out.sort(true); return out;
}
bool Scaredy::saveUserPreset(const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName(name.trim()).substring(0, 60);
    if (clean.isEmpty()) return false;
    auto dir = presetFolder(); if (! dir.createDirectory().wasOk()) return false;
    setProp("preset", clean);
    if (auto xml = apvts.copyState().createXml()) return xml->writeTo(dir.getChildFile(clean + ".xml"));
    return false;
}
bool Scaredy::loadUserPreset(const juce::String& name)
{
    const auto f = presetFolder().getChildFile(juce::File::createLegalFileName(name) + ".xml");
    if (! f.existsAsFile() || f.getSize() > 1000000) return false;
    if (auto xml = juce::XmlDocument::parse(f))
        if (xml->hasTagName(apvts.state.getType()))
        {
            migrateXml(*xml);
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
            loadCurvesFromState();
            return true;
        }
    return false;
}

// ---------- shape library ----------
juce::File Scaredy::shapeFolder() const { return presetFolder().getParentDirectory().getChildFile("shapes"); }
juce::StringArray Scaredy::userShapeNames() const
{
    juce::StringArray out;
    for (auto& f : shapeFolder().findChildFiles(juce::File::findFiles, false, "*.shape")) out.add(f.getFileNameWithoutExtension());
    out.sort(true); return out;
}
bool Scaredy::saveShape(const juce::String& name, const Curve& c)
{
    const auto clean = juce::File::createLegalFileName(name.trim()).substring(0, 60);
    if (clean.isEmpty()) return false;
    auto dir = shapeFolder(); if (! dir.createDirectory().wasOk()) return false;
    return dir.getChildFile(clean + ".shape").replaceWithText(juce::String(curveToString(c)));
}
bool Scaredy::loadShape(const juce::String& name, Curve& out) const
{
    const auto f = shapeFolder().getChildFile(juce::File::createLegalFileName(name) + ".shape");
    if (! f.existsAsFile() || f.getSize() > 100000) return false;
    out = curveFromString(f.loadFileAsString().toStdString()); return true;
}
bool Scaredy::deleteShape(const juce::String& name)
{
    const auto f = shapeFolder().getChildFile(juce::File::createLegalFileName(name) + ".shape");
    return f.existsAsFile() && f.deleteFile();
}
