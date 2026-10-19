#pragma once
#include "Scaredy.h"

// The Scaredy Cat editor: three envelope lanes you draw on, crossover graph, band spectrum, meters and the bottom bar.
// Everything is drawn here (no generic knob page). Layout is designed on a 1180 x 736 canvas and scaled with the window.
class ScaredyEditor : public juce::AudioProcessorEditor
{
public:
    explicit ScaredyEditor(Scaredy&);
    ~ScaredyEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick(const juce::MouseEvent&) override;   // on a wood cheek: back to 100%
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScaredyEditor)
};
