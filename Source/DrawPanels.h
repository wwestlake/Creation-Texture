#pragma once

#include <JuceHeader.h>
#include <Drawing.h>

// The Draw view's own panels (Layout > Draw, docs/REQUIREMENTS.md section 5). The rest of the view - nodes, graph,
// Variables, Properties - is the Image Graph's, shared.

// The canvas: the image being drawn on, large, with the selected Drawing over it as thin lines so its geometry shows
// before it is painted. Mouse wheel zooms, dragging pans, double-click fits it again. The pointer's position is shown
// in drawing coordinates (0..1), which is what scripts use.
class DrawCanvas final : public juce::Component
{
public:
    DrawCanvas();

    void setImage(const juce::Image& image, const juce::String& text);
    void setOverlay(drawing::DrawingPtr drawing, const juce::String& label);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    juce::Rectangle<float> canvasArea() const; // where the canvas is drawn, after zoom and pan
    juce::Rectangle<float> viewArea() const;   // the space left for it
    void showPointer(juce::Point<float> position);

    juce::Image image;
    drawing::DrawingPtr overlay;
    juce::ToggleButton showLines { "Lines" };
    juce::Label info, pointer, overlayLabel;
    float zoom = 1.0f;
    juce::Point<float> pan, dragStart, panAtDragStart;
};

// The script of the selected Draw Script node, in a large editor, with its error underneath.
class ScriptPanel final : public juce::Component
{
public:
    ScriptPanel();

    // The script being edited changed in the editor.
    std::function<void(const juce::String&)> onScriptChanged;

    // Shows a Draw Script node's script, or, with an empty title, a hint to select one.
    void showScript(const juce::String& title, const juce::String& script);
    // The script changed elsewhere (Properties): updates the text unless it is being typed into here.
    void scriptChangedElsewhere(const juce::String& script);
    void setError(const juce::String& error);
    bool isShowingScript() const noexcept { return editor.isVisible(); }

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    juce::Label title, hint, error;
    juce::TextEditor editor;
};
