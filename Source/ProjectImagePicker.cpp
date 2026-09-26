#include "ProjectImagePicker.h"

namespace
{
constexpr int rowHeight = 56;
constexpr int pickerWidth = 380;
constexpr int pickerHeight = 440;
const juce::Colour pickerBackground { 0xff161a1f };

juce::String formatOf(const juce::String& logicalPath)
{
    return logicalPath.fromLastOccurrenceOf(".", false, false).toUpperCase();
}

void drawThumbnail(juce::Graphics& g, const juce::Image& image, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xff111317));
    g.fillRect(area);
    if (image.isValid())
        g.drawImage(image, area.toFloat(), juce::RectanglePlacement::centred);
}
}

ProjectImagePicker::ProjectImagePicker(project_images::Source source,
                                       const juce::String& currentLogicalPath,
                                       std::function<void(const juce::String&)> onImageChosen)
    : images(std::move(source)),
      current(currentLogicalPath),
      onChosen(std::move(onImageChosen))
{
    all = images.list ? images.list() : juce::Array<project_images::Entry>();

    search.setTextToShowWhenEmpty("Search images", juce::Colours::grey);
    search.onTextChange = [this]() { applyFilter(); };
    addAndMakeVisible(search);

    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent(emptyMessage);

    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setColour(juce::ListBox::backgroundColourId, pickerBackground);
    addAndMakeVisible(list);

    applyFilter();
    setSize(pickerWidth, pickerHeight);
}

void ProjectImagePicker::applyFilter()
{
    const auto needle = search.getText().trim();
    shown.clearQuick();
    for (const auto& entry : all)
        if (needle.isEmpty() || entry.displayName.containsIgnoreCase(needle) || entry.logicalPath.containsIgnoreCase(needle))
            shown.add(entry);

    emptyMessage.setText(all.isEmpty() ? "There are no images in this project yet." : "No images match.",
                         juce::dontSendNotification);
    emptyMessage.setVisible(shown.isEmpty());
    list.updateContent();
    list.deselectAllRows();
    for (int i = 0; i < shown.size(); ++i)
        if (shown.getReference(i).logicalPath == current)
            list.selectRow(i);
    list.repaint();
}

void ProjectImagePicker::paint(juce::Graphics& g)
{
    g.fillAll(pickerBackground);
}

void ProjectImagePicker::resized()
{
    auto area = getLocalBounds().reduced(6);
    search.setBounds(area.removeFromTop(26));
    area.removeFromTop(6);
    list.setBounds(area);
    emptyMessage.setBounds(area);
}

int ProjectImagePicker::getNumRows()
{
    return shown.size();
}

void ProjectImagePicker::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow(row, shown.size()))
        return;

    const auto& entry = shown.getReference(row);
    auto area = juce::Rectangle<int>(0, 0, width, height);
    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff2f5d8a));

    // Only rows being drawn ask for a thumbnail, so hundreds of images do not all load when the picker opens.
    const auto thumbnail = images.thumbnail ? images.thumbnail(entry.logicalPath) : juce::Image();
    drawThumbnail(g, thumbnail, area.removeFromLeft(height).reduced(5));

    auto text = area.reduced(8, 5);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(entry.displayName, text.removeFromTop(text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(formatOf(entry.logicalPath), text, juce::Justification::topLeft, true);
}

void ProjectImagePicker::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow(row, shown.size()))
        return;

    if (onChosen)
        onChosen(shown.getReference(row).logicalPath);

    if (auto* callOut = findParentComponentOfClass<juce::CallOutBox>())
        callOut->dismiss();
}

ProjectImageSlot::ProjectImageSlot(project_images::Source source,
                                   const juce::String& currentLogicalPath,
                                   std::function<void(const juce::String&)> onImageChosen)
    : images(std::move(source)),
      current(currentLogicalPath),
      onChosen(std::move(onImageChosen))
{
    if (current.isNotEmpty() && images.list)
        for (const auto& entry : images.list())
            if (entry.logicalPath == current)
                currentName = entry.displayName;
    if (currentName.isEmpty())
        currentName = current.fromLastOccurrenceOf("/", false, false);

    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity(true);
    setTooltip("Click to choose an image from this project");
}

void ProjectImageSlot::paint(juce::Graphics& g)
{
    auto area = getLocalBounds();
    g.setColour(pickerBackground);
    g.fillRoundedRectangle(area.toFloat(), 4.0f);
    g.setColour(isMouseOver() ? juce::Colour(0xff5b8fc7) : juce::Colour(0xff384354));
    g.drawRoundedRectangle(area.toFloat().reduced(0.5f), 4.0f, 1.0f);

    const auto thumbnail = current.isNotEmpty() && images.thumbnail ? images.thumbnail(current) : juce::Image();
    drawThumbnail(g, thumbnail, area.removeFromLeft(area.getHeight()).reduced(6));

    auto text = area.reduced(8, 8);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    if (current.isEmpty())
    {
        g.drawText("No image - click to choose one", text, juce::Justification::centredLeft, true);
        return;
    }

    g.drawText(currentName, text.removeFromTop(text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(12.0f));
    auto details = formatOf(current);
    if (thumbnail.isValid())
        details << "  " << juce::String(thumbnail.getProperties()->getWithDefault("sourceWidth", thumbnail.getWidth()).toString())
                << " x " << juce::String(thumbnail.getProperties()->getWithDefault("sourceHeight", thumbnail.getHeight()).toString());
    g.drawText(details, text, juce::Justification::topLeft, true);
}

void ProjectImageSlot::mouseUp(const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    // Button-style: the picker belongs to this slot, so it opens anchored to it.
    auto picker = std::make_unique<ProjectImagePicker>(images, current, onChosen);
    juce::CallOutBox::launchAsynchronously(std::move(picker), getScreenBounds(), nullptr);
}
