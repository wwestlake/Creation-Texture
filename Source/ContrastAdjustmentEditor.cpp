#include "ContrastAdjustmentEditor.h"

ContrastAdjustmentEditor::ContrastAdjustmentEditor()
{
    setSize(300, 300);
}

ContrastAdjustmentEditor::~ContrastAdjustmentEditor() = default;

void ContrastAdjustmentEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a)); // Dark background

    auto bounds = getLocalBounds().toFloat().reduced(10.0f);
    
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(bounds);

    drawHistogramBackground(g, bounds);
    drawBezierCurve(g, bounds);
}

void ContrastAdjustmentEditor::resized()
{
}

void ContrastAdjustmentEditor::mouseDown(const juce::MouseEvent& event)
{
    auto bounds = getLocalBounds().toFloat().reduced(10.0f);
    auto normalizedPos = juce::Point<float>(
        (event.position.x - bounds.getX()) / bounds.getWidth(),
        1.0f - ((event.position.y - bounds.getY()) / bounds.getHeight())
    );

    float dist1 = normalizedPos.getDistanceFrom(controlPoint1);
    float dist2 = normalizedPos.getDistanceFrom(controlPoint2);

    if (dist1 < 0.1f && dist1 <= dist2)
        draggingPointIndex = 1;
    else if (dist2 < 0.1f)
        draggingPointIndex = 2;
    else
        draggingPointIndex = -1;
}

void ContrastAdjustmentEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (draggingPointIndex == -1) return;

    auto bounds = getLocalBounds().toFloat().reduced(10.0f);
    auto normalizedPos = juce::Point<float>(
        juce::jlimit(0.0f, 1.0f, (event.position.x - bounds.getX()) / bounds.getWidth()),
        juce::jlimit(0.0f, 1.0f, 1.0f - ((event.position.y - bounds.getY()) / bounds.getHeight()))
    );

    if (draggingPointIndex == 1)
        controlPoint1 = normalizedPos;
    else if (draggingPointIndex == 2)
        controlPoint2 = normalizedPos;

    repaint();
}

void ContrastAdjustmentEditor::drawHistogramBackground(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Placeholder histogram drawing
    g.setColour(juce::Colour(0x44888888));
    
    juce::Path histPath;
    histPath.startNewSubPath(bounds.getBottomLeft());
    
    for (float x = 0; x <= 1.0f; x += 0.05f)
    {
        float val = std::abs(std::sin(x * juce::MathConstants<float>::pi)) * 0.5f + 0.1f;
        histPath.lineTo(bounds.getX() + x * bounds.getWidth(), bounds.getBottom() - val * bounds.getHeight());
    }
    
    histPath.lineTo(bounds.getBottomRight());
    histPath.closeSubPath();
    
    g.fillPath(histPath);
}

void ContrastAdjustmentEditor::drawBezierCurve(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    // Convert normalized points back to screen space
    auto pt1 = juce::Point<float>(bounds.getX(), bounds.getBottom());
    auto pt4 = juce::Point<float>(bounds.getRight(), bounds.getY());
    
    auto pt2 = juce::Point<float>(
        bounds.getX() + controlPoint1.x * bounds.getWidth(),
        bounds.getBottom() - controlPoint1.y * bounds.getHeight()
    );
    
    auto pt3 = juce::Point<float>(
        bounds.getX() + controlPoint2.x * bounds.getWidth(),
        bounds.getBottom() - controlPoint2.y * bounds.getHeight()
    );

    // Draw the curve
    juce::Path curve;
    curve.startNewSubPath(pt1);
    curve.cubicTo(pt2, pt3, pt4);

    g.setColour(juce::Colours::white);
    g.strokePath(curve, juce::PathStrokeType(2.0f));

    // Draw control handles
    g.setColour(juce::Colours::grey);
    g.drawLine(pt1.x, pt1.y, pt2.x, pt2.y, 1.0f);
    g.drawLine(pt4.x, pt4.y, pt3.x, pt3.y, 1.0f);

    g.setColour(juce::Colours::cyan);
    g.fillEllipse(pt2.x - 4, pt2.y - 4, 8, 8);
    g.fillEllipse(pt3.x - 4, pt3.y - 4, 8, 8);
}
