#pragma once

#include <JuceHeader.h>

// Choosing an image from the project for a node input (Texture Sample). A project can hold hundreds of images, so
// Properties shows only a compact slot; clicking it opens a selection dialog. In the dialog, clicking a row only
// highlights it (with a larger preview) - the choice is applied only by Use Image / double-click / Enter, and
// Cancel / Escape changes nothing. Thumbnails are fetched only for rows actually drawn. See docs/REQUIREMENTS.md.
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
    // The full image, for the dialog's larger preview.
    std::function<juce::Image(const juce::String& logicalPath)> image;
};
}

class ProjectImagePicker final : public juce::Component,
                                 private juce::ListBoxModel
{
public:
    // onImageChosen receives the chosen logical path, or an empty string when the image is cleared.
    ProjectImagePicker(project_images::Source source,
                       const juce::String& currentLogicalPath,
                       std::function<void(const juce::String&)> onImageChosen);

    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void returnKeyPressed(int lastRowSelected) override;

    void applyFilter();
    juce::String highlightedPath() const;
    void finish(bool apply, const juce::String& path);

    project_images::Source images;
    juce::Array<project_images::Entry> all;
    juce::Array<project_images::Entry> shown;
    juce::String current;
    std::function<void(const juce::String&)> onChosen;

    juce::TextEditor search;
    juce::Label emptyMessage;
    juce::ListBox list;
    juce::Rectangle<int> previewArea;
    juce::Image previewImage;
    juce::String previewName;
    juce::String previewDetails;
    juce::TextButton useButton { "Use Image" };
    juce::TextButton clearButton { "Clear" };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProjectImagePicker)
};

// The one-row slot shown in Properties: the current image's thumbnail, name, and size - or empty. Click to open the
// selection dialog.
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
