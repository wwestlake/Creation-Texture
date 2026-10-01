#include "DrawPanels.h"

namespace
{
const juce::Colour background { 0xff111317 };

void drawChecks(juce::Graphics& g, juce::Rectangle<float> area)
{
    g.saveState();
    g.reduceClipRegion(area.toNearestInt());
    const float cell = 12.0f;
    for (float y = area.getY(); y < area.getBottom(); y += cell)
        for (float x = area.getX(); x < area.getRight(); x += cell)
        {
            const bool dark = (static_cast<int>((x - area.getX()) / cell) + static_cast<int>((y - area.getY()) / cell)) % 2 == 0;
            g.setColour(dark ? juce::Colour(0xff2a2d33) : juce::Colour(0xff34383f));
            g.fillRect(x, y, cell, cell);
        }
    g.restoreState();
}
}

//==============================================================================
DrawCanvas::DrawCanvas()
{
    showLines.setToggleState(true, juce::dontSendNotification);
    showLines.setTooltip("Show the selected drawing's lines over the canvas");
    showLines.onClick = [this]() { repaint(); };
    addAndMakeVisible(showLines);
    for (auto* label : { &info, &pointer, &overlayLabel })
    {
        label->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        label->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(*label);
    }
    pointer.setJustificationType(juce::Justification::centredRight);
}

void DrawCanvas::setImage(const juce::Image& newImage, const juce::String& text)
{
    image = newImage;
    info.setText(text, juce::dontSendNotification);
    repaint();
}

void DrawCanvas::setOverlay(drawing::DrawingPtr drawing, const juce::String& label)
{
    overlay = std::move(drawing);
    overlayLabel.setText(label, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<float> DrawCanvas::viewArea() const
{
    return getLocalBounds().withTrimmedTop(30).withTrimmedBottom(22).reduced(8).toFloat();
}

juce::Rectangle<float> DrawCanvas::canvasArea() const
{
    const auto view = viewArea();
    // The canvas keeps the image's shape (square until there is one).
    const auto shape = image.isValid() ? image.getBounds().toFloat() : juce::Rectangle<float>(0.0f, 0.0f, 1.0f, 1.0f);
    const auto fitted = juce::RectanglePlacement(juce::RectanglePlacement::centred).appliedTo(shape, view);
    return fitted.withSizeKeepingCentre(fitted.getWidth() * zoom, fitted.getHeight() * zoom) + pan;
}

void DrawCanvas::paint(juce::Graphics& g)
{
    g.fillAll(background);
    const auto view = viewArea();
    const auto area = canvasArea();
    g.saveState();
    g.reduceClipRegion(view.toNearestInt());
    drawChecks(g, area);
    if (image.isValid())
    {
        g.setImageResamplingQuality(zoom > 2.0f ? juce::Graphics::lowResamplingQuality : juce::Graphics::mediumResamplingQuality);
        g.drawImage(image, area);
    }
    g.setColour(juce::Colour(0x40ffffff));
    g.drawRect(area, 1.0f);

    if (overlay != nullptr && showLines.getToggleState())
    {
        g.setColour(juce::Colour(0xd040d0ff));
        size_t drawn = 0;
        for (const auto& path : overlay->paths)
        {
            if (path.points.empty() || drawn > 400000)
                continue;
            auto toScreen = [&area](const drawing::Point& p) {
                return juce::Point<float>(area.getX() + p.x * area.getWidth(), area.getY() + p.y * area.getHeight());
            };
            if (path.points.size() == 1)
            {
                const auto c = toScreen(path.points[0]);
                g.fillEllipse(c.x - 1.5f, c.y - 1.5f, 3.0f, 3.0f);
                continue;
            }
            juce::Path p;
            p.startNewSubPath(toScreen(path.points[0]));
            for (size_t i = 1; i < path.points.size(); ++i)
                p.lineTo(toScreen(path.points[i]));
            if (path.closed)
                p.closeSubPath();
            g.strokePath(p, juce::PathStrokeType(1.0f));
            drawn += path.points.size();
        }
    }
    g.restoreState();

    if (! image.isValid() && overlay == nullptr)
    {
        g.setColour(juce::Colours::grey);
        g.drawFittedText("Select a Paint node to see its canvas, or a drawing node to see its lines.", view.toNearestInt(),
                         juce::Justification::centred, 2);
    }
}

void DrawCanvas::resized()
{
    auto top = getLocalBounds().removeFromTop(30).reduced(4);
    showLines.setBounds(top.removeFromLeft(70));
    pointer.setBounds(top.removeFromRight(170));
    overlayLabel.setBounds(top);
    info.setBounds(getLocalBounds().removeFromBottom(22).reduced(8, 0));
}

void DrawCanvas::showPointer(juce::Point<float> position)
{
    const auto area = canvasArea();
    if (! area.contains(position) || area.isEmpty())
    {
        pointer.setText({}, juce::dontSendNotification);
        return;
    }
    const auto x = (position.x - area.getX()) / area.getWidth();
    const auto y = (position.y - area.getY()) / area.getHeight();
    pointer.setText("x " + juce::String(x, 3) + "   y " + juce::String(y, 3), juce::dontSendNotification);
}

void DrawCanvas::mouseMove(const juce::MouseEvent& e) { showPointer(e.position); }
void DrawCanvas::mouseExit(const juce::MouseEvent&) { pointer.setText({}, juce::dontSendNotification); }

void DrawCanvas::mouseDown(const juce::MouseEvent& e)
{
    dragStart = e.position;
    panAtDragStart = pan;
}

void DrawCanvas::mouseDrag(const juce::MouseEvent& e)
{
    pan = panAtDragStart + (e.position - dragStart);
    showPointer(e.position);
    repaint();
}

void DrawCanvas::mouseDoubleClick(const juce::MouseEvent&)
{
    zoom = 1.0f;
    pan = {};
    repaint();
}

void DrawCanvas::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Zoom about the pointer, so what is under it stays under it.
    const auto before = canvasArea();
    const float factor = wheel.deltaY > 0.0f ? 1.15f : (wheel.deltaY < 0.0f ? 1.0f / 1.15f : 1.0f);
    zoom = juce::jlimit(0.25f, 32.0f, zoom * factor);
    const auto after = canvasArea();
    if (! before.isEmpty())
    {
        const float fx = (e.position.x - before.getX()) / before.getWidth();
        const float fy = (e.position.y - before.getY()) / before.getHeight();
        pan += juce::Point<float>(e.position.x - (after.getX() + fx * after.getWidth()), e.position.y - (after.getY() + fy * after.getHeight()));
    }
    showPointer(e.position);
    repaint();
}

//==============================================================================
ScriptPanel::ScriptPanel()
{
    title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);

    hint.setText("Select a Draw Script node (or add one from Nodes > Draw) to write its script here. "
                 "The language is in docs/DRAW_SCRIPT.md.",
                 juce::dontSendNotification);
    hint.setColour(juce::Label::textColourId, juce::Colours::grey);
    hint.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(hint);

    editor.setMultiLine(true, false);
    editor.setReturnKeyStartsNewLine(true);
    editor.setTabKeyUsedAsCharacter(true);
    editor.setScrollbarsShown(true);
    editor.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 14.0f, juce::Font::plain));
    editor.onTextChange = [this]() {
        if (onScriptChanged)
            onScriptChanged(editor.getText());
    };
    addChildComponent(editor);

    error.setColour(juce::Label::textColourId, juce::Colour(0xffff8a80));
    error.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(error);
}

void ScriptPanel::showScript(const juce::String& titleText, const juce::String& script)
{
    const bool has = titleText.isNotEmpty();
    title.setText(has ? titleText : juce::String("Script"), juce::dontSendNotification);
    hint.setVisible(! has);
    editor.setVisible(has);
    if (has)
        editor.setText(script, juce::dontSendNotification);
    error.setText({}, juce::dontSendNotification);
    resized();
}

void ScriptPanel::scriptChangedElsewhere(const juce::String& script)
{
    if (editor.isVisible() && ! editor.hasKeyboardFocus(true) && editor.getText() != script)
        editor.setText(script, juce::dontSendNotification);
}

void ScriptPanel::setError(const juce::String& text)
{
    error.setText(text, juce::dontSendNotification);
}

void ScriptPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));
}

void ScriptPanel::resized()
{
    auto area = getLocalBounds().reduced(6);
    title.setBounds(area.removeFromTop(26));
    area.removeFromTop(4);
    error.setBounds(area.removeFromBottom(40));
    hint.setBounds(area);
    editor.setBounds(area);
}
