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
    generateButton.setColour(juce::TextButton::textColourOffId, Ice::background);
    generateButton.setTitle("Generate five MIDI patterns");
    generateButton.setTooltip("Create five new MIDI variations");
    generateButton.onClick = [this] { generateNow(); };
    addAndMakeVisible(generateButton);
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
    configureLiveControl(mutation, "MUTATION", 0.0);
    configureLiveControl(variation, "VARIATION", 50.0);
    configureLiveControl(density, "DENSITY", 100.0);
    for (int i = 0; i < 5; ++i)
    {
        rows[i] = std::make_unique<MidiPreview>(i + 1);
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
    setSize(840, 790);
}

MainComponent::~MainComponent()
{
    stopTimer();
    worker.reset();
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

void MainComponent::generateNow()
{
    if (busy || dragging || exportOpen) return;
    worker.reset(); busy = true; lastError.clear(); pulse = 0;
    generateButton.setEnabled(false); generateButton.setButtonText("GENERATING");
    for (auto& row : rows) row->setEnabled(false);
    const auto target = selectedTargetKey();
    setStatus(target.isEmpty() ? "Creating five new patterns..." : "Creating five patterns in " + target + "..."); startTimer(300);
    juce::Component::SafePointer<MainComponent> safe(this);
    worker = std::make_unique<GenerationWorker>(*engine, target, [safe](GenerationBatch batch, juce::String error) {
        if (safe) safe->finish(std::move(batch), error);
    });
    worker->startThread();
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
    generateButton.setEnabled(true); generateButton.setButtonText("GENERATE");
    for (auto& row : rows) row->setEnabled(true);
    if (error.isNotEmpty()) { setStatus(error + "  Previous patterns kept. Try again.", true); return; }
    if (batch.clips.size() != 5) { lastError = "Expected five patterns."; setStatus(lastError, true); return; }
    removeOwnedBatch(previousFolder, sessionRoot);
    previousFolder = current.folder;
    current = std::move(batch);
    mutation.setValue(0.0, juce::dontSendNotification);
    variation.setValue(50.0, juce::dontSendNotification);
    density.setValue(100.0, juce::dontSendNotification);
    for (int i = 0; i < 5; ++i) rows[i]->setClip(current.clips[size_t(i)]);
    setStatus("Five new ideas. Drag a pattern into your DAW, or export it.");
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
    control.setName(name); control.setTooltip(name + ": update the generated MIDI in real time.");
    control.onValueChange = [this] { applyLiveControls(); };
    addAndMakeVisible(control);
}

void MainComponent::applyLiveControls()
{
    if (busy || current.clips.empty()) return;
    try
    {
        ::updateLivePreview(current, liveControls());
        for (int i = 0; i < 5; ++i) rows[i]->setClip(current.clips[size_t(i)]);
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
        ::writeLiveMidi(current, liveControls());
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
        safe->exportOpen = false; safe->generateButton.setEnabled(!safe->busy && !safe->dragging);
    });
}

void MainComponent::resized()
{
    const int margin = 32;
    generateButton.setBounds(getWidth() - margin - 210, 47, 210, 46);
    keyChoice.setBounds(margin, 117, 155, 28);
    scaleChoice.setBounds(margin + 165, 117, 120, 28);
    const int controlsY = 181;
    mutation.setBounds(margin, controlsY, 100, 78);
    variation.setBounds(margin + 116, controlsY, 100, 78);
    density.setBounds(margin + 232, controlsY, 100, 78);
    const int top = 312, gap = 10;
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
    g.drawText("LIVE CONTROLS", 32, 158, 160, 12, juce::Justification::left);
    for (auto* control : { &mutation, &variation, &density })
    {
        g.setColour(Ice::muted); g.setFont(Ice::font(8, true));
        g.drawText(control->getName(), control->getX(), 170, control->getWidth(), 12, juce::Justification::centred);
    }
    g.setColour(Ice::line); g.drawHorizontalLine(276, 32, float(getWidth() - 32));
    g.setColour(Ice::blue); g.fillRect(32, 276, 36, 1);
    auto word = Ice::font(32, true); word.setExtraKerningFactor(0.14f); g.setFont(word);
    g.setColour(Ice::white); g.drawText("ICEPUNK", 31, 37, 320, 42, juce::Justification::left);
    g.setFont(Ice::font(10)); g.setColour(Ice::muted);
    g.drawText("M I D I   G E N E R A T O R", 34, 83, 300, 17, juce::Justification::left);
    g.setFont(Ice::font(9));
    g.drawText("FIVE VARIATIONS", 33, 286, 180, 15, juce::Justification::left);
    g.drawText("DRAG TO YOUR DAW", getWidth() - 225, 286, 192, 15, juce::Justification::right);
}
