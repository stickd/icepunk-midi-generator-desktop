#pragma once
#include <JuceHeader.h>
#include <functional>
#include <cstdint>
#include <vector>
#include <array>

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
    bool alignStart = false;
};
struct GenerationBatch { juce::File folder; std::vector<GeneratedClip> clips; };
struct DatasetAnalysis { int filesFound = 0, filesAnalyzed = 0, filesFailed = 0, patterns = 0; juce::File profile; };
GeneratedClip parseMidi(const juce::File&);
void updateLivePreview(GenerationBatch&, LiveControls);
void writeLiveMidi(GenerationBatch&, LiveControls);
GenerationBatch generateSimilarBatch(const GenerationBatch&, const std::array<bool, 5>&, LiveControls,
                                     const juce::File& session, std::function<bool()> cancelled);
void removeOwnedBatch(const juce::File& batch, const juce::File& session);

// Replace this interface with a native engine later; the UI need not change.
class GeneratorEngine
{
public:
    virtual ~GeneratorEngine() = default;
    virtual GenerationBatch generate(juce::String targetKey, juce::File analysisFile, std::function<bool()> cancelled) = 0;
    virtual DatasetAnalysis analyzeDataset(juce::File folder, juce::File profile, std::function<bool()> cancelled) = 0;
};
class PythonGeneratorEngine final : public GeneratorEngine
{
public:
    PythonGeneratorEngine(juce::File root, juce::File session)
        : projectRoot(std::move(root)), sessionRoot(std::move(session)) {}
    GenerationBatch generate(juce::String targetKey, juce::File analysisFile, std::function<bool()>) override;
    DatasetAnalysis analyzeDataset(juce::File folder, juce::File profile, std::function<bool()>) override;
private:
    juce::File projectRoot, sessionRoot;
};
class GenerationWorker final : public juce::Thread
{
public:
    using Completion = std::function<void(GenerationBatch, juce::String)>;
    GenerationWorker(GeneratorEngine& e, juce::String targetKey, juce::File analysisFile, Completion c)
        : Thread("ICEPUNK generation"), engine(e), target(std::move(targetKey)), profile(std::move(analysisFile)), complete(std::move(c)) {}
    void setSimilar(GenerationBatch source, std::array<bool, 5> selected, LiveControls settings, juce::File session)
    { references = std::move(source); selection = selected; controls = settings; sessionRoot = std::move(session); similar = true; }
    ~GenerationWorker() override { signalThreadShouldExit(); stopThread(4000); }
    void run() override;
private:
    GeneratorEngine& engine;
    juce::String target;
    juce::File profile;
    Completion complete;
    GenerationBatch references;
    std::array<bool, 5> selection {};
    LiveControls controls;
    juce::File sessionRoot;
    bool similar = false;
};

class DatasetAnalysisWorker final : public juce::Thread
{
public:
    using Completion = std::function<void(DatasetAnalysis, juce::String)>;
    DatasetAnalysisWorker(GeneratorEngine& e, juce::File folder, juce::File profile, Completion c)
        : Thread("ICEPUNK custom dataset analysis"), engine(e), folder(std::move(folder)), profile(std::move(profile)), complete(std::move(c)) {}
    ~DatasetAnalysisWorker() override { signalThreadShouldExit(); stopThread(5000); }
    void run() override;
private:
    GeneratorEngine& engine;
    juce::File folder, profile;
    Completion complete;
};
