#include "TextureSampleNodePanel.h"

namespace
{
constexpr int rowHeight = 64;
constexpr int titleHeight = 32;
constexpr int panelWidth = 340;
constexpr int maxVisibleRows = 6;
}

TextureSampleNodePanel::TextureSampleNodePanel(juce::Array<ImageChoice> choices,
                                               const juce::String& currentLogicalPath,
                                               std::function<void(const juce::String&)> onImageChosen)
    : images(std::move(choices)),
      onChosen(std::move(onImageChosen))
{
    title.setText("Texture Sample - images in this project", juce::dontSendNotification);
    title.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);

    emptyMessage.setText("There are no images in this project yet.", juce::dontSendNotification);
    emptyMessage.setJustificationType(juce::Justification::centred);
    emptyMessage.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent(emptyMessage);
    emptyMessage.setVisible(images.isEmpty());

    list.setModel(this);
    list.setRowHeight(rowHeight);
    list.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1e2227));
    addAndMakeVisible(list);
    list.setVisible(! images.isEmpty());

    for (int i = 0; i < images.size(); ++i)
        if (images.getReference(i).logicalPath == currentLogicalPath)
            list.selectRow(i);

    const int rows = juce::jlimit(1, maxVisibleRows, images.size());
    setSize(panelWidth, titleHeight + rows * rowHeight + 8);
}

void TextureSampleNodePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2227));
}

void TextureSampleNodePanel::resized()
{
    auto bounds = getLocalBounds().reduced(4);
    title.setBounds(bounds.removeFromTop(titleHeight - 4));
    list.setBounds(bounds);
    emptyMessage.setBounds(bounds);
}

int TextureSampleNodePanel::getNumRows()
{
    return images.size();
}

void TextureSampleNodePanel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected)
{
    if (! juce::isPositiveAndBelow(row, images.size()))
        return;

    const auto& image = images.getReference(row);
    auto area = juce::Rectangle<int>(0, 0, width, height);

    if (rowIsSelected)
        g.fillAll(juce::Colour(0xff2f5d8a));

    auto thumbArea = area.removeFromLeft(height).reduced(6);
    g.setColour(juce::Colour(0xff111317));
    g.fillRect(thumbArea);
    if (image.thumbnail.isValid())
        g.drawImage(image.thumbnail, thumbArea.toFloat(), juce::RectanglePlacement::centred);

    auto textArea = area.reduced(8, 6);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(14.0f));
    g.drawText(image.displayName, textArea.removeFromTop(textArea.getHeight() / 2), juce::Justification::bottomLeft, true);
    g.setColour(juce::Colours::lightgrey);
    g.setFont(juce::FontOptions(12.0f));
    g.drawText(image.details, textArea, juce::Justification::topLeft, true);
}

void TextureSampleNodePanel::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (! juce::isPositiveAndBelow(row, images.size()))
        return;

    if (onChosen)
        onChosen(images.getReference(row).logicalPath);

    if (auto* callOut = findParentComponentOfClass<juce::CallOutBox>())
        callOut->dismiss();
}
