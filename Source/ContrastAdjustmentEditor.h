#pragma once

#include <JuceHeader.h>

class ContrastAdjustmentEditor : public juce::Component
{
public:
    ContrastAdjustmentEditor();
    ~ContrastAdjustmentEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;

private:
    void drawHistogramBackground(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawBezierCurve(juce::Graphics& g, juce::Rectangle<float> bounds);

    // Curve control points [0.0 - 1.0] normalized
    juce::Point<float> controlPoint1 { 0.25f, 0.25f };
    juce::Point<float> controlPoint2 { 0.75f, 0.75f };
    
    int draggingPointIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ContrastAdjustmentEditor)
};
