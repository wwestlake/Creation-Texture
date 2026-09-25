#pragma once

#include <JuceHeader.h>

// The panel a Texture Sample node opens on double-click: the images in the project that the node can use, and
// nothing else. Picking one hands its logical path back and closes the panel. See docs/REQUIREMENTS.md section 2.
class TextureSampleNodePanel final : public juce::Component,
                                     private juce::ListBoxModel
{
public:
    struct ImageChoice
    {
        juce::String displayName;
        juce::String logicalPath;
        juce::Image thumbnail;
        juce::String details;
    };

    TextureSampleNodePanel(juce::Array<ImageChoice> choices,
                           const juce::String& currentLogicalPath,
                           std::function<void(const juce::String&)> onImageChosen);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    juce::Array<ImageChoice> images;
    std::function<void(const juce::String&)> onChosen;
    juce::Label title;
    juce::Label emptyMessage;
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TextureSampleNodePanel)
};
