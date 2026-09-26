#pragma once

#include <JuceHeader.h>

// The project's images, with thumbnails, for picking the one a node uses. Lives inside the Properties panel of a
// node whose input takes an image (Texture Sample) - assets are always chosen through the thing that uses them,
// never a general browser. See docs/REQUIREMENTS.md sections 1-2.
class ProjectImageList final : public juce::Component,
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

    ProjectImageList(juce::Array<ImageChoice> choices,
                     const juce::String& currentLogicalPath,
                     std::function<void(const juce::String&)> onImageChosen);

    // Height that shows every image without scrolling, up to a cap.
    int getPreferredHeight() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;

    juce::Array<ImageChoice> images;
    std::function<void(const juce::String&)> onChosen;
    juce::Label emptyMessage;
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectImageList)
};
