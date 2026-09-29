#pragma once
#include <JuceHeader.h>
#include <functional>
#include <vector>

struct PreviewNote { int pitch; float start; float duration; int velocity; };
struct GeneratedClip { juce::File file; std::vector<PreviewNote> notes; double beats = 0; };
struct GenerationBatch { juce::File folder; std::vector<GeneratedClip> clips; };
GeneratedClip parseMidi(const juce::File&);
void removeOwnedBatch(const juce::File& batch, const juce::File& session);

// Replace this interface with a native engine later; the UI need not change.
class GeneratorEngine
{
public:
    virtual ~GeneratorEngine() = default;
    virtual GenerationBatch generate(std::function<bool()> cancelled) = 0;
};
class PythonGeneratorEngine final : public GeneratorEngine
{
public:
    PythonGeneratorEngine(juce::File root, juce::File session)
        : projectRoot(std::move(root)), sessionRoot(std::move(session)) {}
    GenerationBatch generate(std::function<bool()>) override;
private:
    juce::File projectRoot, sessionRoot;
};
class GenerationWorker final : public juce::Thread
{
public:
    using Completion = std::function<void(GenerationBatch, juce::String)>;
    GenerationWorker(GeneratorEngine& e, Completion c)
        : Thread("ICEPUNK generation"), engine(e), complete(std::move(c)) {}
    ~GenerationWorker() override { signalThreadShouldExit(); stopThread(4000); }
    void run() override;
private:
    GeneratorEngine& engine;
    Completion complete;
};
