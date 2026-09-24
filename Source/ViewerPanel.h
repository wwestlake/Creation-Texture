
#pragma once
#include <JuceHeader.h>
#include "ViewerNodeEditor.h"

class ViewerPanel final : public juce::Component
{
public:
    ViewerPanel();
    ~ViewerPanel() override;

    void resized() override;
    ViewerNodeEditor* getViewer() { return &viewer; }

private:
    ViewerNodeEditor viewer;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ViewerPanel)
};
