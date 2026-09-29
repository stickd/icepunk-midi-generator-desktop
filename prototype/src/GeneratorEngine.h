#pragma once
#include <JuceHeader.h>
#include <functional>
#include <cstdint>
#include <vector>

struct PreviewNote { int pitch; float start; float duration; int velocity; };
struct LiveControls { float mutation = 0.0f, variation = 0.5f, density = 1.0f; };
struct GeneratedClip
{
    juce::File file;
    std::vector<PreviewNote> notes, baseNotes;
    juce::MemoryBlock baseFileData;
    juce::String key;
    double bpm = 146.0, beats = 0.0;
    uint32_t seed = 0;
};
struct GenerationBatch { juce::File folder; std::vector<GeneratedClip> clips; };
GeneratedClip parseMidi(const juce::File&);
void applyLiveControls(GenerationBatch&, LiveControls);
void removeOwnedBatch(const juce::File& batch, const juce::File& session);

// Replace this interface with a native engine later; the UI need not change.
class GeneratorEngine
{
public:
    virtual ~GeneratorEngine() = default;
    virtual GenerationBatch generate(juce::String targetKey, std::function<bool()> cancelled) = 0;
};
class PythonGeneratorEngine final : public GeneratorEngine
{
public:
    PythonGeneratorEngine(juce::File root, juce::File session)
        : projectRoot(std::move(root)), sessionRoot(std::move(session)) {}
    GenerationBatch generate(juce::String targetKey, std::function<bool()>) override;
private:
    juce::File projectRoot, sessionRoot;
};
class GenerationWorker final : public juce::Thread
{
public:
    using Completion = std::function<void(GenerationBatch, juce::String)>;
    GenerationWorker(GeneratorEngine& e, juce::String targetKey, Completion c)
        : Thread("ICEPUNK generation"), engine(e), target(std::move(targetKey)), complete(std::move(c)) {}
    ~GenerationWorker() override { signalThreadShouldExit(); stopThread(4000); }
    void run() override;
private:
    GeneratorEngine& engine;
    juce::String target;
    Completion complete;
};
