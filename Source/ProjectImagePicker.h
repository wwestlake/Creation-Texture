#pragma once

#include <JuceHeader.h>

// Choosing an image from the project for a node input (Texture Sample). A project can hold hundreds of images, so
// Properties shows only a compact slot; clicking it opens a fixed-size, searchable picker beside the slot.
// Thumbnails are fetched only for rows that are actually drawn. See docs/REQUIREMENTS.md sections 1-2.
namespace project_images
{
struct Entry
{
    juce::String displayName;
    juce::String logicalPath;
};

struct Source
{
    std::function<juce::Array<Entry>()> list;
    // A small preview for one image; the caller caches. Invalid if the image cannot be read.
    std::function<juce::Image(const juce::String& logicalPath)> thumbnail;
};
}

class ProjectImagePicker final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    ProjectImagePicker(project_images::Source source,
                       const juce::String& currentLogicalPath,
                       std::function<void(const juce::String&)> onImageChosen);

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void applyFilter();

    project_images::Source images;
    juce::Array<project_images::Entry> all;
    juce::Array<project_images::Entry> shown;
    juce::String current;
    std::function<void(const juce::String&)> onChosen;

    juce::TextEditor search;
    juce::Label emptyMessage;
    juce::ListBox list;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectImagePicker)
};

// The one-row slot shown in Properties: the current image's thumbnail, name, and size. Click to change it.
class ProjectImageSlot final : public juce::Component,
                               public juce::SettableTooltipClient
{
public:
    ProjectImageSlot(project_images::Source source,
                     const juce::String& currentLogicalPath,
                     std::function<void(const juce::String&)> onImageChosen);

    static constexpr int preferredHeight = 64;

    void paint(juce::Graphics& g) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    project_images::Source images;
    juce::String current;
    juce::String currentName;
    std::function<void(const juce::String&)> onChosen;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectImageSlot)
};
