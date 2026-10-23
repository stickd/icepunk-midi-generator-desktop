#pragma once
#include "MidiPreview.h"
#include "PresetManager.h"
#include <array>

class IceLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    juce::Font getTextButtonFont(juce::TextButton&, int height) override { return Ice::font(height > 30 ? 13.0f : 9.0f, true); }
};

class MainComponent final : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent(juce::File project);
    ~MainComponent() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void generateNow();
    bool isBusy() const { return busy || analysisBusy; }
    const GenerationBatch& getBatch() const { return current; }
    juce::String getError() const { return lastError; }
    juce::File getSessionRoot() const { return sessionRoot; }
    // Test seam: exercise the same UI failure path without changing the real generator.
    void setEngineForTest(std::unique_ptr<GeneratorEngine> replacement);
    void exercisePresetSelectionForTest();
    void setLiveControlsForTest(float mutation, float variation, float density);
    void setBatchForTest(GenerationBatch batch) { current = std::move(batch); }
    void toggleReferenceForTest(int index) { toggleReference(index); }
    void clearReferencesForTest() { clearReferences(); }
    int referenceCountForTest() const { return referenceCount(); }
    juce::String generateTextForTest() const { return generateButton.getButtonText(); }
private:
    void timerCallback() override;
    void finish(GenerationBatch, juce::String);
    void toggleReference(int index);
    void clearReferences();
    void updateReferenceUI();
    int referenceCount() const;
    void exportMidi(const juce::File&);
    void setStatus(juce::String, bool error = false);
    IceLookAndFeel look;
    juce::File projectRoot, outputRoot, sessionRoot;
    std::unique_ptr<GeneratorEngine> engine;
    std::unique_ptr<GenerationWorker> worker;
    std::unique_ptr<DatasetAnalysisWorker> analysisWorker;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TextButton generateButton { "GENERATE" }, clearReferencesButton { "CLEAR" };
    juce::Label referenceStatus;
    std::array<bool, 5> selectedReferences {};
    bool similarInProgress = false;
    juce::ComboBox keyChoice, scaleChoice;
    juce::ComboBox datasetMode, presetChoice;
    PresetManager presets;
    juce::Array<juce::File> presetFiles;
    juce::File activePresetFile, activePresetAnalysis;
    std::unique_ptr<juce::AlertWindow> presetDialog;
    juce::TextButton savePresetButton { "SAVE CURRENT AS PRESET" }, openPresetsButton { "OPEN PRESETS FOLDER" };
    void refreshPresets();
    void selectPreset();
    void saveCurrentPreset();
    void updatePresetControls();
    juce::File currentAnalysisFile() const;
    juce::TextButton chooseFolderButton { "CHOOSE FOLDER" }, analyzeButton { "ANALYZE" };
    juce::Label customFolder;
    juce::Slider mutation, variation, density;
    std::array<std::unique_ptr<MidiPreview>, 5> rows;
    juce::Label status;
    juce::TooltipWindow tips { this, 700 };
    GenerationBatch current;
    juce::File previousFolder;
    juce::String lastError;
    bool busy = false, analysisBusy = false, exportOpen = false, dragging = false, liveWritePending = false;
    juce::File customFolderPath, customProfile;
    int pulse = 0;
    juce::String selectedTargetKey() const;
    void updateScaleChoice();
    void updateDatasetMode();
    void chooseCustomFolder();
    void analyzeCustomFolder();
    void finishDatasetAnalysis(DatasetAnalysis, juce::String);
    void applyLiveControls();
    void flushLiveMidi();
    LiveControls liveControls() const;
    void configureLiveControl(juce::Slider&, juce::String name, double initial);
};
