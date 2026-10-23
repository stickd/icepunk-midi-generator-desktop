#pragma once
#include <JuceHeader.h>

// Analysis remains the analyzer's JSON object. The Python bridge consumes it unchanged.
struct AnalysisPreset
{
    juce::String name;
    juce::var analysis;
    juce::File file;
};

class PresetManager
{
public:
    PresetManager();
    explicit PresetManager(juce::File directory);
    juce::File getPresetsDirectory() const { return directory; }
    juce::Array<juce::File> getPresets() const;
    AnalysisPreset savePreset(juce::String name, const juce::var& analysis) const;
    AnalysisPreset loadPreset(const juce::File&) const;
    static juce::var readAnalysis(const juce::File&);
    static void validateAnalysis(const juce::var&);
    static void writeAnalysis(const juce::File&, const juce::var&);
private:
    juce::File directory;
};
