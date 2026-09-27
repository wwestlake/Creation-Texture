#include "ProjectImagePicker.h"

namespace
{
constexpr int rowHeight = 56;
constexpr int dialogWidth = 640;
constexpr int dialogHeight = 480;
constexpr int listWidth = 330;
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

    search.setTextToShowWhenEmpty("Filter by name", juce::Colours::grey);
    search.onTextChange = [this]() { applyFilter(); };
    search.onReturnKey = [this]() { finish(highlightedPath().isNotEmpty(), highlightedPath()); };
    search.onEscapeKey = [this]() { finish(false, {}); };
    addAndMakeVisible(search);

    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent(emptyMessage);

    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setColour(juce::ListBox::backgroundColourId, pickerBackground);
    addAndMakeVisible(list);

    useButton.onClick = [this]() { finish(true, highlightedPath()); };
    clearButton.onClick = [this]() { finish(true, {}); };
    cancelButton.onClick = [this]() { finish(false, {}); };
    clearButton.setEnabled(current.isNotEmpty());
    addAndMakeVisible(useButton);
    addAndMakeVisible(clearButton);
    addAndMakeVisible(cancelButton);

    setWantsKeyboardFocus(true);
    applyFilter();
    setSize(dialogWidth, dialogHeight);
}

void ProjectImagePicker::applyFilter()
{
    const auto keep = highlightedPath().isNotEmpty() ? highlightedPath() : current;
    const auto needle = search.getText().trim();

    shown.clearQuick();
    for (const auto& entry : all)
        if (needle.isEmpty() || entry.displayName.containsIgnoreCase(needle) || entry.logicalPath.containsIgnoreCase(needle))
            shown.add(entry);

    emptyMessage.setText(all.isEmpty() ? "There are no images in this project yet." : "No images match the filter.",
                         juce::dontSendNotification);
    emptyMessage.setVisible(shown.isEmpty());
    list.updateContent();

    // Keep the highlighted image highlighted if it still matches the filter.
    list.deselectAllRows();
    for (int i = 0; i < shown.size(); ++i)
        if (shown.getReference(i).logicalPath == keep)
            list.selectRow(i);
    selectedRowsChanged(list.getSelectedRow());
    list.repaint();
}

juce::String ProjectImagePicker::highlightedPath() const
{
    const int row = list.getSelectedRow();
    return juce::isPositiveAndBelow(row, shown.size()) ? shown.getReference(row).logicalPath : juce::String();
}

void ProjectImagePicker::finish(bool apply, const juce::String& path)
{
    if (apply && onChosen)
        onChosen(path);

    if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
        dialog->exitModalState(apply ? 1 : 0);
}

void ProjectImagePicker::paint(juce::Graphics& g)
{
    g.fillAll(pickerBackground);

    g.setColour(juce::Colour(0xff111317));
    g.fillRect(previewArea);
    if (previewImage.isValid())
    {
        auto imageArea = previewArea.reduced(8).withTrimmedBottom(44);
        g.drawImage(previewImage, imageArea.toFloat(), juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);

        auto text = previewArea.reduced(10, 6).removeFromBottom(40);
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(previewName, text.removeFromTop(20), juce::Justification::centredLeft, true);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(previewDetails, text, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour(juce::Colours::grey);
        g.setFont(juce::FontOptions(13.0f));
        g.drawText("Click an image to preview it", previewArea, juce::Justification::centred, true);
    }
}

void ProjectImagePicker::resized()
{
    auto area = getLocalBounds().reduced(10);

    auto buttons = area.removeFromBottom(30);
    cancelButton.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    useButton.setBounds(buttons.removeFromRight(110));
    clearButton.setBounds(buttons.removeFromLeft(90));
    area.removeFromBottom(10);

    auto left = area.removeFromLeft(listWidth);
    search.setBounds(left.removeFromTop(26));
    left.removeFromTop(6);
    list.setBounds(left);
    emptyMessage.setBounds(left);

    area.removeFromLeft(10);
    previewArea = area;
}

bool ProjectImagePicker::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        finish(false, {});
        return true;
    }
    return false;
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

    // Only rows being drawn ask for a thumbnail, so hundreds of images do not all load when the dialog opens.
    const auto thumbnail = images.thumbnail ? images.thumbnail(entry.logicalPath) : juce::Image();
    drawThumbnail(g, thumbnail, area.removeFromLeft(height).reduced(5));

    auto text = area.reduced(8, 5);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(entry.displayName, text.removeFromTop(text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(formatOf(entry.logicalPath) + (entry.logicalPath == current ? "  (current)" : ""),
               text, juce::Justification::topLeft, true);
}

void ProjectImagePicker::selectedRowsChanged(int)
{
    const auto path = highlightedPath();
    useButton.setEnabled(path.isNotEmpty());

    previewImage = {};
    previewName.clear();
    previewDetails.clear();
    if (path.isNotEmpty())
    {
        const auto& entry = shown.getReference(list.getSelectedRow());
        previewImage = images.image ? images.image(path) : juce::Image();
        previewName = entry.displayName;
        previewDetails = formatOf(path);
        if (previewImage.isValid())
            previewDetails << "  " << previewImage.getWidth() << " x " << previewImage.getHeight();
        else
            previewDetails << "  (could not be read)";
    }
    repaint(previewArea);
}

void ProjectImagePicker::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow(row, shown.size()))
        finish(true, shown.getReference(row).logicalPath);
}

void ProjectImagePicker::returnKeyPressed(int)
{
    if (highlightedPath().isNotEmpty())
        finish(true, highlightedPath());
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
        g.setColour(juce::Colours::lightgrey);
        g.drawText("No image - click to choose one", text, juce::Justification::centredLeft, true);
        return;
    }

    g.drawText(currentName, text.removeFromTop(text.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(12.0f));
    auto details = formatOf(current);
    if (thumbnail.isValid())
        details << "  " << thumbnail.getProperties()->getWithDefault("sourceWidth", thumbnail.getWidth()).toString()
                << " x " << thumbnail.getProperties()->getWithDefault("sourceHeight", thumbnail.getHeight()).toString();
    g.drawText(details, text, juce::Justification::topLeft, true);
}

void ProjectImageSlot::mouseUp(const juce::MouseEvent& e)
{
    if (! e.mouseWasClicked())
        return;

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Choose an image";
    options.content.setOwned(new ProjectImagePicker(images, current, onChosen));
    options.componentToCentreAround = getTopLevelComponent();
    options.dialogBackgroundColour = pickerBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* dialog = options.launchAsync())
        dialog->setResizeLimits(520, 360, 2000, 1600);
}
