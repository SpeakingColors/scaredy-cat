// Plugin entry point.
#include "common/FxBase.h"
#include "scaredy/Scaredy.h"
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new Scaredy(); }
