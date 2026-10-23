#include "MainComponent.h"
#include "RuntimePaths.h"

void IceLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                        const juce::Colour& colour, bool over, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced(0.5f);
    if (button.getHeight() > 30)
    {
        auto c = !button.isEnabled() ? juce::Colour(0xff29414d) : down ? Ice::blue.darker(0.2f) : over ? Ice::white : Ice::blue;
        g.setGradientFill(juce::ColourGradient(c, r.getTopLeft(), c.darker(0.12f), r.getBottomLeft(), false));
        g.fillRoundedRectangle(r, 4);
        g.setColour(Ice::white.withAlpha(0.35f)); g.drawRoundedRectangle(r, 4, 1);
    }
    else if (over && button.isEnabled())
    { g.setColour(Ice::blue.withAlpha(0.09f)); g.fillRoundedRectangle(r, 3); }
}

MainComponent::MainComponent(juce::File project) : projectRoot(std::move(project))
{
    setLookAndFeel(&look);
    outputRoot = icepunkDataDirectory().getChildFile("sessions");
    sessionRoot = outputRoot.getChildFile("session-" + juce::Uuid().toString());
    // The app is single-instance. Remove only app-owned sessions left after a crash.
    for (auto folder : outputRoot.findChildFiles(juce::File::findDirectories, false, "session-*"))
        if (folder.getParentDirectory() == outputRoot && folder.getChildFile(".icepunk-session").existsAsFile())
            folder.deleteRecursively();
    auto result = sessionRoot.createDirectory();
    if (result.wasOk()) sessionRoot.getChildFile(".icepunk-session").replaceWithText("ICEPUNK prototype session\n");
    engine = std::make_unique<PythonGeneratorEngine>(projectRoot, sessionRoot);
    customProfile = icepunkDataDirectory().getChildFile("custom-datasets/current/profile.json");
    generateButton.setColour(juce::TextButton::textColourOffId, Ice::background);
    generateButton.setTitle("Generate five MIDI patterns");
    generateButton.setTooltip("Create five new MIDI variations");
    generateButton.onClick = [this] { generateNow(); };
    addAndMakeVisible(generateButton);
    clearReferencesButton.setTooltip("Clear reference selections");
    clearReferencesButton.onClick = [this] { clearReferences(); };
    addAndMakeVisible(clearReferencesButton);
    referenceStatus.setFont(Ice::font(10));
    referenceStatus.setColour(juce::Label::textColourId, Ice::muted);
    addAndMakeVisible(referenceStatus);
    updateReferenceUI();
    keyChoice.addItem("RANDOM KEY", 1);
    const juce::StringArray roots { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    for (int i = 0; i < roots.size(); ++i) keyChoice.addItem(roots[i], i + 2);
    keyChoice.setSelectedId(1);
    keyChoice.setTooltip("Choose a key, or leave Random Key for the dataset's natural variety.");
    keyChoice.onChange = [this] { updateScaleChoice(); };
    addAndMakeVisible(keyChoice);
    scaleChoice.addItem("MINOR", 1); scaleChoice.addItem("MAJOR", 2);
    scaleChoice.setSelectedId(1);
    scaleChoice.setTooltip("Choose the scale used when you choose a key.");
    addAndMakeVisible(scaleChoice);
    updateScaleChoice();
    datasetMode.addItem("FACTORY MIDI", 1); datasetMode.addItem("CUSTOM MIDI", 2);
    datasetMode.setSelectedId(1);
    datasetMode.setTooltip("Factory MIDI uses the included dataset. Custom MIDI uses your analyzed MIDI folder.");
    datasetMode.onChange = [this] {
        activePresetFile = juce::File(); activePresetAnalysis = juce::File();
        presetChoice.setSelectedId(1, juce::dontSendNotification);
        updateDatasetMode();
    };
    addAndMakeVisible(datasetMode);
    chooseFolderButton.onClick = [this] { chooseCustomFolder(); };
    analyzeButton.onClick = [this] { analyzeCustomFolder(); };
    chooseFolderButton.setTooltip("Choose a local folder containing MIDI files.");
    analyzeButton.setTooltip("Analyze this folder into a local ICEPUNK custom profile.");
    addAndMakeVisible(chooseFolderButton); addAndMakeVisible(analyzeButton);
    customFolder.setFont(Ice::font(9)); customFolder.setColour(juce::Label::textColourId, Ice::muted);
    customFolder.setJustificationType(juce::Justification::centredLeft);
    customFolder.setText(customProfile.existsAsFile() ? "Saved custom profile ready. Choose a folder to update it."
                                                       : "Choose a folder, then analyze it.", juce::dontSendNotification);
    addAndMakeVisible(customFolder);
    presetChoice.setTooltip("Load a saved analysis without analyzing the source MIDI again.");
    presetChoice.onChange = [this] { selectPreset(); };
    savePresetButton.onClick = [this] { saveCurrentPreset(); };
    openPresetsButton.onClick = [this] {
        try { presets.getPresets(); presets.getPresetsDirectory().revealToUser(); refreshPresets(); }
        catch (const std::exception& e) { setStatus(e.what(), true); }
    };
    addAndMakeVisible(presetChoice); addAndMakeVisible(savePresetButton); addAndMakeVisible(openPresetsButton);
    updateDatasetMode();
    configureLiveControl(mutation, "MUTATION", 0.0);
    configureLiveControl(variation, "VARIATION", 50.0);
    configureLiveControl(density, "DENSITY", 100.0);
    for (int i = 0; i < 5; ++i)
    {
        rows[i] = std::make_unique<MidiPreview>(i + 1);
        rows[i]->onToggleReference = [this, i] { toggleReference(i); };
        rows[i]->onExport = [this](const juce::File& f) { exportMidi(f); };
        rows[i]->onError = [this](juce::String text) { setStatus(text, true); };
        rows[i]->onDragActivity = [this](bool active) {
            if (active) flushLiveMidi();
            dragging = active; generateButton.setEnabled(!busy && !active && !exportOpen);
        };
        addAndMakeVisible(*rows[i]);
    }
    status.setFont(Ice::font(11));
    status.setColour(juce::Label::textColourId, Ice::muted);
    status.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(status);
    setStatus(result.failed() ? "Cannot create output folder: " + result.getErrorMessage()
                            : "Press Generate. Find your next idea.", result.failed());
    refreshPresets();
    setSize(840, 870);
}

MainComponent::~MainComponent()
{
    stopTimer();
    worker.reset();
    analysisWorker.reset();
    presetDialog.reset();
    chooser.reset();
    if (sessionRoot.getParentDirectory() == outputRoot && sessionRoot.getFileName().startsWith("session-")
        && sessionRoot.getChildFile(".icepunk-session").existsAsFile())
        sessionRoot.deleteRecursively();
    setLookAndFeel(nullptr);
}

void MainComponent::setEngineForTest(std::unique_ptr<GeneratorEngine> replacement)
{
    jassert(!busy); worker.reset(); engine = std::move(replacement);
}

void MainComponent::setLiveControlsForTest(float newMutation, float newVariation, float newDensity)
{
    mutation.setValue(newMutation * 100.0, juce::dontSendNotification);
    variation.setValue(newVariation * 100.0, juce::dontSendNotification);
    density.setValue(newDensity * 100.0, juce::dontSendNotification);
    applyLiveControls();
    flushLiveMidi();
}

void MainComponent::setStatus(juce::String text, bool error)
{
    status.setText(text, juce::dontSendNotification);
    status.setTooltip(text);
    status.setColour(juce::Label::textColourId, error ? juce::Colour(0xffe7a99b) : Ice::muted);
}

int MainComponent::referenceCount() const
{
    return int(std::count(selectedReferences.begin(), selectedReferences.end(), true));
}

void MainComponent::updateReferenceUI()
{
    const int count = referenceCount();
    if (!busy) generateButton.setButtonText(count ? "GENERATE SIMILAR" : "GENERATE");
    referenceStatus.setText(count ? juce::String(count) + (count == 1 ? " reference selected" : " references selected") : juce::String(), juce::dontSendNotification);
    clearReferencesButton.setVisible(count > 0);
    clearReferencesButton.setEnabled(!busy);
    for (int i = 0; i < 5; ++i) if (rows[size_t(i)]) rows[size_t(i)]->setReferenceSelected(selectedReferences[size_t(i)]);
}

void MainComponent::toggleReference(int index)
{
    if (busy || analysisBusy || dragging || exportOpen || current.clips.size() != 5) return;
    auto& selected = selectedReferences[size_t(index)];
    if (!selected && referenceCount() == 3) { setStatus("Choose up to three references. Clear or deselect one first."); return; }
    flushLiveMidi();
    selected = !selected;
    updateReferenceUI();
    updateDatasetMode();
}

void MainComponent::clearReferences()
{
    if (busy || dragging || exportOpen) return;
    selectedReferences.fill(false);
    updateReferenceUI();
    updateDatasetMode();
}

void MainComponent::generateNow()
{
    if (busy || analysisBusy || dragging || exportOpen || presetDialog) return;
    const bool custom = datasetMode.getSelectedId() == 2 && activePresetFile == juce::File();
    if (custom && referenceCount() == 0 && !customProfile.existsAsFile())
    {
        setStatus("Analyze a MIDI folder before generating Custom MIDI.", true);
        return;
    }
    flushLiveMidi();
    similarInProgress = referenceCount() > 0;
    worker.reset(); busy = true; lastError.clear(); pulse = 0;
    updateReferenceUI();
    updatePresetControls();
    generateButton.setEnabled(false); generateButton.setButtonText("GENERATING");
    for (auto& row : rows) row->setEnabled(false);
    const auto target = selectedTargetKey();
    setStatus(custom ? "Creating five Custom MIDI patterns..." : (target.isEmpty() ? "Creating five new patterns..." : "Creating five patterns in " + target + "...")); startTimer(300);
    juce::Component::SafePointer<MainComponent> safe(this);
    worker = std::make_unique<GenerationWorker>(*engine, target, activePresetFile != juce::File() ? activePresetAnalysis : (custom ? customProfile : juce::File()), [safe](GenerationBatch batch, juce::String error) {
        if (safe) safe->finish(std::move(batch), error);
    });
    if (similarInProgress) worker->setSimilar(current, selectedReferences, liveControls(), sessionRoot);
    worker->startThread();
}

void MainComponent::updateDatasetMode()
{
    const bool custom = datasetMode.getSelectedId() == 2;
    chooseFolderButton.setVisible(custom); analyzeButton.setVisible(custom); customFolder.setVisible(custom);
    generateButton.setEnabled(!busy && !analysisBusy && !dragging && !exportOpen && (referenceCount() > 0 || activePresetFile != juce::File() || !custom || customProfile.existsAsFile()));
    updatePresetControls();
    repaint();
}

void MainComponent::chooseCustomFolder()
{
    if (busy || analysisBusy || exportOpen || presetDialog) return;
    auto initial = customFolderPath.isDirectory() ? customFolderPath : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser>("Choose MIDI dataset folder", initial);
    juce::Component::SafePointer<MainComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safe](const juce::FileChooser& fc) {
            if (!safe) return;
            auto folder = fc.getResult();
            if (folder.isDirectory())
            {
                safe->customFolderPath = folder;
                safe->customFolder.setText("Not analyzed: " + folder.getFullPathName(), juce::dontSendNotification);
                safe->setStatus("Click Analyze to use this folder. Until then, Generate uses the previously saved profile.");
                safe->analyzeButton.setEnabled(true);
            }
        });
}

void MainComponent::analyzeCustomFolder()
{
    if (presetDialog || dragging || exportOpen) return;
    if (busy || analysisBusy || !customFolderPath.isDirectory())
    {
        if (!customFolderPath.isDirectory()) setStatus("Choose a folder containing MIDI files first.", true);
        return;
    }
    analysisWorker.reset(); analysisBusy = true;
    updatePresetControls();
    chooseFolderButton.setEnabled(false); analyzeButton.setEnabled(false); analyzeButton.setButtonText("ANALYZING");
    generateButton.setEnabled(false);
    setStatus("Analyzing Custom MIDI folder in the background...");
    juce::Component::SafePointer<MainComponent> safe(this);
    analysisWorker = std::make_unique<DatasetAnalysisWorker>(*engine, customFolderPath, customProfile,
        [safe](DatasetAnalysis result, juce::String error) { if (safe) safe->finishDatasetAnalysis(std::move(result), error); });
    analysisWorker->startThread();
}

void MainComponent::finishDatasetAnalysis(DatasetAnalysis result, juce::String error)
{
    analysisBusy = false; analyzeButton.setButtonText("ANALYZE"); chooseFolderButton.setEnabled(true); analyzeButton.setEnabled(true);
    if (error.isNotEmpty()) { setStatus(error, true); updateDatasetMode(); return; }
    customProfile = result.profile;
    activePresetFile = juce::File(); activePresetAnalysis = juce::File();
    presetChoice.setSelectedId(1, juce::dontSendNotification);
    customFolder.setText("Ready: " + juce::String(result.filesAnalyzed) + " MIDI, " + juce::String(result.patterns)
                         + " patterns" + (result.filesFailed ? ", " + juce::String(result.filesFailed) + " skipped" : ""),
                         juce::dontSendNotification);
    setStatus("Custom MIDI profile is ready. Generate now uses your analyzed folder.");
    updateDatasetMode();
}

void MainComponent::timerCallback()
{
    if (!busy)
    {
        flushLiveMidi();
        return;
    }
    ++pulse;
    generateButton.setButtonText("GENERATING" + juce::String::repeatedString(".", pulse % 4));
}

void MainComponent::finish(GenerationBatch batch, juce::String error)
{
    stopTimer(); busy = false; liveWritePending = false; lastError = error;
    updatePresetControls();
    generateButton.setEnabled(true); updateReferenceUI();
    for (auto& row : rows) row->setEnabled(true);
    if (error.isNotEmpty()) { setStatus(error + "  Previous patterns kept. Try again.", true); return; }
    if (batch.clips.size() != 5) { lastError = "Expected five patterns."; setStatus(lastError, true); return; }
    removeOwnedBatch(previousFolder, sessionRoot);
    previousFolder = current.folder;
    current = std::move(batch);
    // Keep the user's controls across Generate and apply them to the new base phrase.
    applyLiveControls();
    flushLiveMidi();
    for (int i = 0; i < 5; ++i) rows[i]->setClip(current.clips[size_t(i)]);
    setStatus(similarInProgress ? "Similar patterns ready. Selected references were kept." : "Five new ideas. Drag a pattern into your DAW, or export it.");
    repaint();
}

void MainComponent::configureLiveControl(juce::Slider& control, juce::String name, double initial)
{
    control.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    control.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
    control.setRange(0.0, 100.0, 0.1); control.setValue(initial);
    control.setNumDecimalPlacesToDisplay(1);
    control.setTextValueSuffix("%");
    control.setColour(juce::Slider::rotarySliderFillColourId, Ice::blue);
    control.setColour(juce::Slider::rotarySliderOutlineColourId, Ice::line);
    control.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    control.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    control.setColour(juce::Slider::textBoxTextColourId, Ice::white);
    control.setName(name);
    control.setDoubleClickReturnValue(true, initial);
    if (name == "MUTATION") control.setTooltip("How many notes change while staying in key. 0% restores the original. Double-click to reset.");
    else if (name == "VARIATION") control.setTooltip("Changes the strength and character of Mutation. Has no effect when Mutation is 0%. Double-click to reset.");
    else control.setTooltip("Keep fewer notes: 100% is the original density, 0% keeps one note repeated every two bars. Double-click to reset.");
    control.onValueChange = [this] { applyLiveControls(); };
    addAndMakeVisible(control);
}

void MainComponent::applyLiveControls()
{
    if (busy || current.clips.empty()) return;
    try
    {
        for (int i = 0; i < 5; ++i)
        {
            if (!selectedReferences[size_t(i)])
            {
                GenerationBatch single {current.folder, {current.clips[size_t(i)]}};
                ::updateLivePreview(single, liveControls());
                current.clips[size_t(i)] = std::move(single.clips[0]);
            }
            rows[i]->setClip(current.clips[size_t(i)]);
        }
        liveWritePending = true;
        // Debounce disk writes while the user is still dragging a control.
        startTimer(80);
    }
    catch (const std::exception& error) { setStatus(error.what(), true); }
}

LiveControls MainComponent::liveControls() const
{
    return { float(mutation.getValue() / 100.0), float(variation.getValue() / 100.0),
             float(density.getValue() / 100.0) };
}

void MainComponent::flushLiveMidi()
{
    if (!liveWritePending || busy || current.clips.empty()) return;
    try
    {
        GenerationBatch unlocked {current.folder, {}};
        for (int i = 0; i < 5; ++i) if (!selectedReferences[size_t(i)]) unlocked.clips.push_back(current.clips[size_t(i)]);
        ::writeLiveMidi(unlocked, liveControls());
        liveWritePending = false;
        stopTimer();
        setStatus("Live controls update the five MIDI patterns. Return a control to its old value to restore that state.");
    }
    catch (const std::exception& error) { liveWritePending = false; stopTimer(); setStatus(error.what(), true); }
}

void MainComponent::exportMidi(const juce::File& source)
{
    if (busy || exportOpen || dragging) return;
    flushLiveMidi();
    exportOpen = true; generateButton.setEnabled(false);
    auto initial = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("ICEPUNK_" + source.getFileName());
    chooser = std::make_unique<juce::FileChooser>("Export ICEPUNK MIDI", initial, "*.mid");
    juce::Component::SafePointer<MainComponent> safe(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
        | juce::FileBrowserComponent::warnAboutOverwriting, [safe, source](const juce::FileChooser& fc) {
        if (!safe) return;
        auto target = fc.getResult();
        if (target != juce::File())
        {
            if (!target.hasFileExtension("mid;midi")) target = target.withFileExtension("mid");
            if (target == source || source.copyFileTo(target)) safe->setStatus("Exported " + target.getFileName());
            else safe->setStatus("Export failed. Choose another location.", true);
        }
        safe->exportOpen = false; safe->updateDatasetMode();
    });
}

void MainComponent::resized()
{
    const int margin = 32;
    generateButton.setBounds(getWidth() - margin - 210, 47, 210, 46);
    referenceStatus.setBounds(getWidth() - margin - 210, 96, 155, 18);
    clearReferencesButton.setBounds(getWidth() - margin - 54, 95, 54, 20);
    keyChoice.setBounds(margin, 117, 155, 28);
    scaleChoice.setBounds(margin + 165, 117, 120, 28);
    datasetMode.setBounds(margin, 163, 170, 28);
    chooseFolderButton.setBounds(margin + 180, 163, 130, 28);
    analyzeButton.setBounds(margin + 320, 163, 95, 28);
    customFolder.setBounds(margin + 425, 163, getWidth() - margin - 425, 28);
    const int presetX = margin + 370, presetWidth = getWidth() - margin - presetX;
    presetChoice.setBounds(presetX, 225, presetWidth, 28);
    savePresetButton.setBounds(presetX, 259, presetWidth, 26);
    openPresetsButton.setBounds(presetX, 288, presetWidth, 24);
    const int controlsY = 226;
    mutation.setBounds(margin, controlsY, 100, 78);
    variation.setBounds(margin + 116, controlsY, 100, 78);
    density.setBounds(margin + 232, controlsY, 100, 78);
    const int top = 357, gap = 10;
    const int rowHeight = (getHeight() - top - 46 - 4 * gap) / 5;
    for (int i = 0; i < 5; ++i) rows[i]->setBounds(margin, top + i * (rowHeight + gap), getWidth() - margin * 2, rowHeight);
    status.setBounds(margin - 3, getHeight() - 36, getWidth() - 2 * margin + 6, 22);
}

juce::String MainComponent::selectedTargetKey() const
{
    if (keyChoice.getSelectedId() == 1) return {};
    return keyChoice.getText() + " " + scaleChoice.getText().toLowerCase();
}

void MainComponent::updateScaleChoice()
{
    const bool random = keyChoice.getSelectedId() == 1;
    scaleChoice.setEnabled(!random);
    scaleChoice.setAlpha(random ? 0.45f : 1.0f);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Ice::background);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff132633), 380, -100, Ice::background, 480, 155, true));
    g.fillRect(getLocalBounds());
    g.setColour(Ice::muted); g.setFont(Ice::font(9, true));
    g.drawText("KEY / SCALE", 32, 103, 160, 12, juce::Justification::left);
    g.setColour(Ice::muted); g.setFont(Ice::font(9, true));
    g.drawText("DATASET", 32, 149, 160, 12, juce::Justification::left);
    g.drawText("PRESET", 402, 203, 160, 12, juce::Justification::left);
    g.drawText("LIVE CONTROLS", 32, 203, 160, 12, juce::Justification::left);
    for (auto* control : { &mutation, &variation, &density })
    {
        g.setColour(Ice::muted); g.setFont(Ice::font(8, true));
        g.drawText(control->getName(), control->getX(), 215, control->getWidth(), 12, juce::Justification::centred);
    }
    g.setColour(Ice::line); g.drawHorizontalLine(321, 32, float(getWidth() - 32));
    g.setColour(Ice::blue); g.fillRect(32, 321, 36, 1);
    auto word = Ice::font(32, true); word.setExtraKerningFactor(0.14f); g.setFont(word);
    g.setColour(Ice::white); g.drawText("ICEPUNK", 31, 37, 320, 42, juce::Justification::left);
    g.setFont(Ice::font(10)); g.setColour(Ice::muted);
    g.drawText("M I D I   G E N E R A T O R", 34, 83, 300, 17, juce::Justification::left);
    g.setFont(Ice::font(9));
    g.drawText("FIVE VARIATIONS", 33, 331, 180, 15, juce::Justification::left);
    g.drawText("DRAG TO YOUR DAW", getWidth() - 225, 331, 192, 15, juce::Justification::right);
}


juce::File MainComponent::currentAnalysisFile() const
{
    if (activePresetFile != juce::File()) return activePresetAnalysis;
    if (datasetMode.getSelectedId() == 2) return customProfile;
    // Source builds and PyInstaller's cross-platform resource layouts.
    for (const auto& relative : { "analysis_output/midi_analysis.json",
                                "engine/_internal/resources/analysis_output/midi_analysis.json",
                                "engine/resources/analysis_output/midi_analysis.json" })
    {
        auto file = projectRoot.getChildFile(relative);
        if (file.existsAsFile()) return file;
    }
    return {};
}

void MainComponent::updatePresetControls()
{
    const bool available = !busy && !analysisBusy && !exportOpen && !dragging && !presetDialog;
    presetChoice.setEnabled(available);
    savePresetButton.setEnabled(available && currentAnalysisFile().existsAsFile());
    openPresetsButton.setEnabled(available);
    datasetMode.setEnabled(available);
}

void MainComponent::refreshPresets()
{
    try
    {
        presetFiles = presets.getPresets();
        presetChoice.clear(juce::dontSendNotification);
        presetChoice.addItem("Default / Current Dataset", 1);
        int selected = 1;
        for (int i = 0; i < presetFiles.size(); ++i)
        {
            // Keep corrupted imports selectable so loading explains the error.
            auto label = presetFiles[i].getFileNameWithoutExtension();
            try { label = presets.loadPreset(presetFiles[i]).name; }
            catch (const std::exception&) { label += " (invalid)"; }
            presetChoice.addItem(label, i + 2);
            if (presetFiles[i] == activePresetFile) selected = i + 2;
        }
        // A loaded snapshot stays usable even if its preset was removed externally.
        if (activePresetFile != juce::File() && selected == 1)
        {
            presetFiles.add(activePresetFile);
            selected = presetFiles.size() + 1;
            presetChoice.addItem(activePresetFile.getFileNameWithoutExtension() + " (loaded)", selected);
        }
        presetChoice.setSelectedId(selected, juce::dontSendNotification);
    }
    catch (const std::exception& e) { setStatus(e.what(), true); }
    updatePresetControls();
}

void MainComponent::selectPreset()
{
    if (busy || analysisBusy || exportOpen || dragging || presetDialog) { refreshPresets(); return; }
    const int index = presetChoice.getSelectedId() - 2;
    if (index < 0)
    {
        activePresetFile = juce::File(); activePresetAnalysis = juce::File();
        updateDatasetMode();
        setStatus("Using the current dataset. Press Generate for new patterns.");
        return;
    }
    try
    {
        if (index >= presetFiles.size()) throw std::runtime_error("Preset is no longer available. Refresh the presets folder.");
        auto preset = presets.loadPreset(presetFiles[index]);
        auto snapshot = sessionRoot.getChildFile("active-preset.json");
        // Commit selection only after a validated, atomic snapshot is available.
        PresetManager::writeAnalysis(snapshot, preset.analysis);
        activePresetFile = preset.file; activePresetAnalysis = snapshot;
        updateDatasetMode();
        setStatus("Loaded preset: " + preset.name + ". Press Generate for new patterns.");
    }
    catch (const std::exception& e) { refreshPresets(); setStatus(e.what(), true); }
}

void MainComponent::saveCurrentPreset()
{
    if (busy || analysisBusy || exportOpen || dragging || presetDialog) return;
    try
    {
        auto analysis = PresetManager::readAnalysis(currentAnalysisFile());
        presetDialog = std::make_unique<juce::AlertWindow>("Save Current as Preset", "Enter a unique preset name (up to 80 characters).", juce::MessageBoxIconType::NoIcon, this);
        presetDialog->addTextEditor("name", "", "Preset name:");
        presetDialog->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        presetDialog->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        updatePresetControls();
        juce::Component::SafePointer<MainComponent> safe(this);
        presetDialog->enterModalState(true, juce::ModalCallbackFunction::create([safe, analysis](int result) {
            if (!safe) return;
            auto name = safe->presetDialog->getTextEditorContents("name");
            safe->presetDialog.reset();
            if (result == 1)
            {
                try
                {
                    auto preset = safe->presets.savePreset(name, analysis);
                    auto snapshot = safe->sessionRoot.getChildFile("active-preset.json");
                    PresetManager::writeAnalysis(snapshot, preset.analysis);
                    safe->activePresetFile = preset.file; safe->activePresetAnalysis = snapshot;
                    safe->setStatus("Saved preset: " + preset.name);
                }
                catch (const std::exception& e) { safe->setStatus(e.what(), true); }
            }
            safe->refreshPresets(); safe->updateDatasetMode();
        }), false);
    }
    catch (const std::exception& e) { setStatus(e.what(), true); }
}


void MainComponent::exercisePresetSelectionForTest()
{
    auto analysis = PresetManager::readAnalysis(currentAnalysisFile());
    auto saved = presets.savePreset("Smoke-" + juce::Uuid().toString(), analysis);
    refreshPresets();
    presetChoice.setSelectedId(presetFiles.indexOf(saved.file) + 2, juce::dontSendNotification);
    selectPreset();
    if (activePresetFile != saved.file || !activePresetAnalysis.existsAsFile())
        throw std::runtime_error("Preset selection did not activate the saved analysis.");
    auto snapshot = activePresetAnalysis.loadFileAsString();
    saved.file.replaceWithText("{corrupted");
    selectPreset();
    if (activePresetFile != saved.file || activePresetAnalysis.loadFileAsString() != snapshot)
        throw std::runtime_error("Failed preset load replaced the previous active analysis.");
    // Generation must use its loaded snapshot, even after the source preset disappears.
    saved.file.deleteFile();
    refreshPresets();
}
