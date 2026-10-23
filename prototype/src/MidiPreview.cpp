#include "MidiPreview.h"

MidiPreview::MidiPreview(int number) : index(number)
{
    setTitle("MIDI " + juce::String(index));
    reference.setTitle("Select MIDI " + juce::String(index) + " as reference");
    reference.setTooltip("Select this pattern as a reference for Generate Similar.");
    reference.onClick = [this] { if (onToggleReference) onToggleReference(); };
    reference.setEnabled(false);
    addAndMakeVisible(reference);
    setDescription("Drag this MIDI pattern to your DAW, or use Export.");
    exportButton.setTooltip("Save this MIDI file");
    exportButton.setTitle("Export MIDI " + juce::String(index));
    exportButton.setColour(juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
    exportButton.setColour(juce::TextButton::textColourOffId, Ice::muted);
    exportButton.setEnabled(false);
    exportButton.onClick = [this] { if (onExport && clip.file.existsAsFile()) onExport(clip.file); };
    addAndMakeVisible(exportButton);
}

void MidiPreview::setClip(const GeneratedClip& value)
{
    clip = value;
    exportButton.setEnabled(clip.file.existsAsFile());
    reference.setEnabled(clip.file.existsAsFile());
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    repaint();
}

void MidiPreview::resized()
{
    reference.setBounds(6, getHeight() / 2 - 12, 24, 24);
    exportButton.setBounds(getWidth() - 71, getHeight() / 2 + 2, 60, 24);
}

void MidiPreview::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(Ice::panel); g.fillRoundedRectangle(bounds, 5);
    g.setColour(hovered && !clip.notes.empty() ? Ice::blue.withAlpha(0.55f) : Ice::line);
    g.drawRoundedRectangle(bounds, 5, 1);
    g.setColour(Ice::muted); g.setFont(Ice::font(9, true));
    g.drawText("MIDI", 37, 15, 46, 12, juce::Justification::centredLeft);
    g.setColour(clip.notes.empty() ? Ice::muted : Ice::white);
    g.setFont(Ice::font(24));
    g.drawText(juce::String(index).paddedLeft('0', 2), 36, 28, 48, 29, juce::Justification::centredLeft);
    if (clip.key.isNotEmpty())
    {
        g.setColour(Ice::blue.withAlpha(0.85f)); g.setFont(Ice::font(9, true));
        // At the minimum window height a row is short enough that anchoring
        // this label to its bottom would overlap the two-digit MIDI number.
        const int keyY = juce::jmax(60, getHeight() - 23);
        g.drawText(clip.key.toUpperCase(), 35, keyY, 58, 13, juce::Justification::centredLeft);
    }

    juce::Rectangle<float> roll(100, 10, float(getWidth() - 184), float(getHeight() - 20));
    int low = 48, high = 84;
    if (!clip.notes.empty())
    {
        low = 127; high = 0;
        for (auto n : clip.notes) { low = juce::jmin(low, n.pitch); high = juce::jmax(high, n.pitch); }
        const int padding = juce::jmax(2, (12 - (high - low)) / 2);
        low -= padding; high += padding;
    }
    auto grid = roll.withTrimmedTop(12);
    for (int beat = 0; beat <= 16; ++beat)
    {
        float x = grid.getX() + grid.getWidth() * beat / 16.0f;
        g.setColour(beat % 4 == 0 ? juce::Colour(0xff2a3b48) : juce::Colour(0xff18232c));
        g.drawVerticalLine(int(x), grid.getY(), grid.getBottom());
        if (beat < 16 && beat % 4 == 0)
        {
            g.setColour(Ice::muted.withAlpha(0.65f)); g.setFont(Ice::font(8));
            g.drawText(juce::String(beat / 4 + 1), int(x + 3), int(roll.getY() - 1), 24, 10, juce::Justification::left);
        }
    }
    for (int j = 0; j <= 4; ++j)
    {
        g.setColour(juce::Colour(0xff15212a));
        g.drawHorizontalLine(int(grid.getY() + grid.getHeight() * j / 4), grid.getX(), grid.getRight());
    }
    if (clip.notes.empty())
    {
        g.setFont(Ice::font(10)); g.setColour(Ice::muted.withAlpha(0.40f));
        g.drawText("AWAITING SIGNAL", grid, juce::Justification::centred);
    }
    else
    {
        const float rowHeight = grid.getHeight() / float(high - low + 1);
        for (auto n : clip.notes)
        {
            auto x = grid.getX() + n.start / 16.0f * grid.getWidth();
            auto y = grid.getY() + (high - n.pitch) * rowHeight;
            auto w = juce::jmax(2.0f, n.duration / 16.0f * grid.getWidth() - 0.8f);
            auto h = juce::jlimit(1.8f, 4.0f, rowHeight - 0.3f);
            auto note = juce::Rectangle<float>(x, y, w, h);
            g.setColour(Ice::blue.withAlpha(0.07f)); g.fillRoundedRectangle(note.expanded(1.5f), 2);
            g.setColour(Ice::blue.withAlpha(0.48f + n.velocity / 127.0f * 0.50f));
            g.fillRoundedRectangle(note, 0.7f);
        }
    }
    const int cx = getWidth() - 40, cy = getHeight() / 2 - 9;
    g.setColour(clip.notes.empty() ? Ice::muted.withAlpha(0.25f) : Ice::blue);
    for (int x = -3; x <= 3; x += 6)
        for (int y = -5; y <= 5; y += 5)
            g.fillEllipse(float(cx + x), float(cy + y), 2, 2);
}

void MidiPreview::mouseDrag(const juce::MouseEvent& event)
{
    if (dragged || !isEnabled() || !clip.file.existsAsFile() || event.getDistanceFromDragStart() < 5) return;
    dragged = true;
    if (onDragActivity) onDragActivity(true);
    juce::Component::SafePointer<MidiPreview> safe(this);
    bool started = juce::DragAndDropContainer::performExternalDragDropOfFiles(
        {clip.file.getFullPathName()}, false, this, [safe] {
            if (safe && safe->onDragActivity) safe->onDragActivity(false);
        });
    if (!started)
    {
        if (onDragActivity) onDragActivity(false);
        if (onError) onError("Drag unavailable. Use the row's Export button.");
    }
}
