#include "MainComponent.h"
#include "RuntimePaths.h"
#include "AppConfig.h"

class FailingEngine final : public GeneratorEngine
{
    GenerationBatch generate(juce::String, juce::File, std::function<bool()>) override { throw std::runtime_error("Simulated generation failure"); }
    DatasetAnalysis analyzeDataset(juce::File, juce::File, std::function<bool()>) override { throw std::runtime_error("Simulated analysis failure"); }
};

// Runs the real async UI/engine/parser path. No test notes are displayed as generated content.
class SmokeTest final : private juce::Timer
{
public:
    SmokeTest(MainComponent& c, juce::File root, juce::File output)
        : component(c), project(std::move(root)), destination(std::move(output))
    { destination.createDirectory(); startTimer(30); }
private:
    MainComponent& component;
    juce::File project, destination, savedFolder;
    juce::String firstHash;
    int stage = 0, ticks = 0;
    void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    juce::String fingerprint(const GenerationBatch& batch)
    {
        juce::MemoryBlock data;
        for (const auto& clip : batch.clips) { juce::MemoryBlock bytes; clip.file.loadFileAsData(bytes); data.append(bytes.getData(), bytes.getSize()); }
        return data.toBase64Encoding();
    }
    void verify()
    {
        require(component.getError().isEmpty(), component.getError().toRawUTF8());
        const auto& batch = component.getBatch();
        require(batch.clips.size() == 5, "Expected five previews");
        for (const auto& clip : batch.clips)
        {
            auto parsed = parseMidi(clip.file);
            require(parsed.notes.size() == clip.notes.size(), "Preview note mismatch");
            require(parsed.beats == 16, "Wrong MIDI length");
        }
        require(component.getSessionRoot().findChildFiles(juce::File::findDirectories, false, "batch-*").size() <= 2,
                "Unbounded batch accumulation");
    }
    void timerCallback() override
    {
        try
        {
            if (++ticks > 2000) throw std::runtime_error("UI smoke test timeout");
            if (component.isBusy()) return;
            switch (stage++)
            {
                case 0: component.generateNow(); break;
                case 1:
                    verify(); firstHash = fingerprint(component.getBatch()); component.generateNow(); break;
                case 2:
                    verify(); require(firstHash != fingerprint(component.getBatch()), "Repeated batch is identical");
                    savedFolder = component.getBatch().folder;
                    firstHash = fingerprint(component.getBatch());
                    component.setEngineForTest(std::make_unique<FailingEngine>()); component.generateNow(); break;
                case 3:
                    require(component.getError().contains("Simulated"), "Failure was not reported");
                    require(component.getBatch().folder == savedFolder && fingerprint(component.getBatch()) == firstHash,
                            "Failure destroyed previous MIDI");
                    component.setEngineForTest(std::make_unique<PythonGeneratorEngine>(project, component.getSessionRoot()));
                    component.generateNow(); break;
                case 4:
                {
                    verify();
                    const auto original = fingerprint(component.getBatch());
                    component.setLiveControlsForTest(0.85f, 0.8f, 0.55f);
                    verify(); require(original != fingerprint(component.getBatch()), "Live controls did not change MIDI");
                    component.setLiveControlsForTest(0.0f, 0.5f, 1.0f);
                    verify(); require(original == fingerprint(component.getBatch()), "Live controls did not restore MIDI");
                    for (int i = 0; i < 5; ++i)
                    {
                        auto target = destination.getChildFile("ICEPUNK_test_0" + juce::String(i + 1) + ".mid");
                        require(component.getBatch().clips[size_t(i)].file.copyFileTo(target), "Export copy failed");
                        parseMidi(target);
                    }
                    auto screenshot = component.createComponentSnapshot(component.getLocalBounds(), true, 2.0f);
                    auto stream = destination.getChildFile("ICEPUNK-preview.png").createOutputStream();
                    require(stream != nullptr, "Screenshot write failed");
                    juce::PNGImageFormat png; png.writeImageToStream(screenshot, *stream); stream.reset();
                    destination.getChildFile("native-smoke-test.txt").replaceWithText(
                        "PASS: three real five-file batches; native preview parser; 16 beats each; different repeated batches; "
                        "simulated failure preserves previous five files; recovery; deterministic live controls; export of each file; maximum two retained batches.\n");
                    stopTimer(); juce::JUCEApplication::getInstance()->setApplicationReturnValue(0); juce::JUCEApplication::quit();
                    break;
                }
            }
        }
        catch (const std::exception& error)
        {
            destination.getChildFile("native-smoke-test.txt").replaceWithText("FAIL: " + juce::String(error.what()));
            stopTimer(); juce::JUCEApplication::getInstance()->setApplicationReturnValue(1); juce::JUCEApplication::quit();
        }
    }
};

class IceWindow final : public juce::DocumentWindow
{
public:
    explicit IceWindow(MainComponent* content)
        : DocumentWindow("ICEPUNK", Ice::background, DocumentWindow::minimiseButton | DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true); setContentOwned(content, true);
        setResizable(true, true);
        setResizeLimits(780, 820, 1800, 1600);
        centreWithSize(getWidth(), getHeight()); setVisible(true);
    }
    void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
};

class IceApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ICEPUNK MIDI Generator"; }
    const juce::String getApplicationVersion() override { return ICEPUNK_VERSION; }
    bool moreThanOneInstanceAllowed() override { return false; }
    void initialise(const juce::String& commandLine) override
    {
        auto args = juce::StringArray::fromTokens(commandLine, true); args.removeEmptyStrings();
        for (auto& arg : args) arg = arg.unquoted();
        auto logFolder = icepunkDataDirectory().getChildFile("logs");
        logFolder.createDirectory();
        logger = std::make_unique<juce::FileLogger>(logFolder.getChildFile("app.log"), "ICEPUNK " ICEPUNK_VERSION, 262144);
        juce::Logger::setCurrentLogger(logger.get());
        auto root = icepunkRuntimeRoot();
       #if ICEPUNK_DEVELOPMENT_ENGINE
        auto projectIndex = args.indexOf("--project");
        if (projectIndex >= 0 && projectIndex + 1 < args.size()) root = juce::File(args[projectIndex + 1]);
       #endif
        auto* content = new MainComponent(root);
        window = std::make_unique<IceWindow>(content);
        auto testIndex = args.indexOf("--self-test");
        if (testIndex >= 0 && testIndex + 1 < args.size())
            smoke = std::make_unique<SmokeTest>(*content, root, juce::File(args[testIndex + 1]));
    }
    void shutdown() override { smoke.reset(); window.reset(); juce::Logger::setCurrentLogger(nullptr); logger.reset(); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted(const juce::String&) override { if (window) window->toFront(true); }
private:
    std::unique_ptr<IceWindow> window;
    std::unique_ptr<SmokeTest> smoke;
    std::unique_ptr<juce::FileLogger> logger;
};

START_JUCE_APPLICATION(IceApplication)
