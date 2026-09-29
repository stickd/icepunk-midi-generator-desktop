#pragma once
#include "GeneratorEngine.h"

namespace Ice
{
    inline const juce::Colour background { 0xff080c10 }, panel { 0xff0d141b }, line { 0xff23323d };
    inline const juce::Colour white { 0xffe7f3fa }, blue { 0xff9cdaef }, muted { 0xff758997 };
    inline juce::Font font(float size, bool bold = false)
    { return juce::Font(juce::FontOptions("Segoe UI", size, bold ? juce::Font::bold : juce::Font::plain)); }
}

class MidiPreview final : public juce::Component
{
public:
    explicit MidiPreview(int number);
    void setClip(const GeneratedClip&);
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent&) override { hovered = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hovered = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override { dragged = false; }
    void mouseDrag(const juce::MouseEvent&) override;
    std::function<void(bool)> onDragActivity;
    std::function<void(const juce::File&)> onExport;
    std::function<void(juce::String)> onError;
private:
    int index;
    GeneratedClip clip;
    bool hovered = false, dragged = false;
    juce::TextButton exportButton { "EXPORT" };
};
