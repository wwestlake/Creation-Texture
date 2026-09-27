#include "ImageLabPanel.h"

#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>

namespace
{
const juce::Colour panelBackground { 0xff1e2227 };
const juce::Colour listBackground { 0xff161a1f };
constexpr int layerRowHeight = 52;
constexpr int eyeWidth = 28;
const juce::String layerDragId = "image-lab-layer";

juce::String slugFor(const juce::String& name)
{
    auto slug = name.trim().toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-_ ").replace(" ", "-");
    while (slug.contains("--"))
        slug = slug.replace("--", "-");
    slug = slug.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    return slug.isNotEmpty() ? slug : "image";
}

void drawCheckerboard(juce::Graphics& g, juce::Rectangle<int> area, int cell)
{
    g.saveState();
    g.reduceClipRegion(area);
    for (int y = area.getY(); y < area.getBottom(); y += cell)
        for (int x = area.getX(); x < area.getRight(); x += cell)
        {
            const bool dark = (((x - area.getX()) / cell) + ((y - area.getY()) / cell)) % 2 == 0;
            g.setColour(dark ? juce::Colour(0xff4a4a4a) : juce::Colour(0xff6a6a6a));
            g.fillRect(x, y, cell, cell);
        }
    g.restoreState();
}
}

// Shows the composite: fitted to the view to start, mouse wheel zooms, drag pans. Transparency shows as a checkerboard.
class ImageLabWorkspace::Canvas final : public juce::Component
{
public:
    explicit Canvas(ImageLabDocument& d) : document(d) {}

    void resetView()
    {
        zoom = 0.0f;
        offset = {};
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111317));

        const auto image = document.getComposite();
        if (! image.isValid())
        {
            g.setColour(juce::Colours::grey);
            g.setFont(juce::FontOptions(15.0f));
            const auto message = document.getLastError().isNotEmpty()
                                   ? document.getLastError()
                                   : juce::String("Add an image layer to start (Add Image... in the Layers panel).");
            g.drawFittedText(message, getLocalBounds().reduced(20), juce::Justification::centred, 4);
            return;
        }

        const auto area = imageArea(image);
        drawCheckerboard(g, area.getSmallestIntegerContainer(), 12);
        g.setImageResamplingQuality(effectiveZoom(image) >= 2.0f ? juce::Graphics::lowResamplingQuality
                                                                  : juce::Graphics::mediumResamplingQuality);
        g.drawImage(image, area);

        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(juce::String(image.getWidth()) + " x " + juce::String(image.getHeight()) + "   "
                       + juce::String(juce::roundToInt(effectiveZoom(image) * 100.0f)) + "%",
                   getLocalBounds().removeFromBottom(20).reduced(8, 0), juce::Justification::centredLeft);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        const auto image = document.getComposite();
        if (! image.isValid())
            return;

        const float before = effectiveZoom(image);
        const float after = juce::jlimit(0.05f, 32.0f, before * (wheel.deltaY > 0 ? 1.15f : 1.0f / 1.15f));
        // Zoom about the pointer.
        const auto centre = getLocalBounds().toFloat().getCentre() + offset;
        offset += (e.position - centre) * (1.0f - after / before);
        zoom = after;
        repaint();
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        offset += e.position - lastDrag;
        lastDrag = e.position;
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override { lastDrag = e.position; }
    void mouseDoubleClick(const juce::MouseEvent&) override { resetView(); }

private:
    float fitZoom(const juce::Image& image) const
    {
        const auto bounds = getLocalBounds().reduced(16).toFloat();
        return juce::jmin(bounds.getWidth() / static_cast<float>(image.getWidth()),
                          bounds.getHeight() / static_cast<float>(image.getHeight()), 1.0f);
    }

    float effectiveZoom(const juce::Image& image) const { return zoom > 0.0f ? zoom : fitZoom(image); }

    juce::Rectangle<float> imageArea(const juce::Image& image) const
    {
        const float z = effectiveZoom(image);
        const auto size = juce::Point<float>(static_cast<float>(image.getWidth()) * z, static_cast<float>(image.getHeight()) * z);
        const auto centre = getLocalBounds().toFloat().getCentre() + offset;
        return { centre.x - size.x / 2.0f, centre.y - size.y / 2.0f, size.x, size.y };
    }

    ImageLabDocument& document;
    float zoom = 0.0f; // 0 = fit to view
    juce::Point<float> offset;
    juce::Point<float> lastDrag;
};

// The layer stack, top layer first. Click a row to make it active, click the eye to show/hide, drag to reorder.
class ImageLabLayerList final : public juce::Component,
                                       private juce::ListBoxModel,
                                       public juce::DragAndDropTarget
{
public:
    explicit ImageLabLayerList(ImageLabDocument& d) : document(d)
    {
        list.setModel(this);
        list.setRowHeight(layerRowHeight);
        list.setColour(juce::ListBox::backgroundColourId, listBackground);
        addAndMakeVisible(list);
    }

    void update()
    {
        list.updateContent();
        const int active = document.getActiveIndex();
        if (active >= 0)
            list.selectRow(rowForIndex(active), false, true);
        else
            list.deselectAllRows();
        list.repaint();
    }

    void resized() override { list.setBounds(getLocalBounds()); }

    void paintOverChildren(juce::Graphics& g) override
    {
        if (dropRow < 0)
            return;
        const int y = juce::jlimit(0, getHeight() - 2, dropRow * layerRowHeight - list.getViewport()->getViewPositionY());
        g.setColour(juce::Colour(0xff5b8fc7));
        g.fillRect(0, y - 1, getWidth(), 3);
    }

    // Drag and drop within the list.
    bool isInterestedInDragSource(const SourceDetails& details) override { return details.description == layerDragId; }
    void itemDragMove(const SourceDetails& details) override { setDropRow(details.localPosition.y); }
    void itemDragExit(const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped(const SourceDetails& details) override
    {
        setDropRow(details.localPosition.y);
        const int fromIndex = document.getActiveIndex();
        // A drop line between rows r-1 and r puts the layer where row r's layer is, counting from the top.
        int targetRow = dropRow;
        const int fromRow = rowForIndex(fromIndex);
        if (targetRow > fromRow)
            --targetRow;
        dropRow = -1;
        repaint();
        document.moveLayer(fromIndex, indexForRow(juce::jlimit(0, document.getNumLayers() - 1, targetRow)));
    }

private:
    int rowForIndex(int index) const { return document.getNumLayers() - 1 - index; }
    int indexForRow(int row) const { return document.getNumLayers() - 1 - row; }

    void setDropRow(int y)
    {
        const int scrolled = y + list.getViewport()->getViewPositionY();
        dropRow = juce::jlimit(0, document.getNumLayers(), (scrolled + layerRowHeight / 2) / layerRowHeight);
        repaint();
    }

    int getNumRows() override { return document.getNumLayers(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        const int index = indexForRow(row);
        const auto* layer = document.getLayer(index);
        if (layer == nullptr)
            return;

        auto area = juce::Rectangle<int>(0, 0, width, height);
        g.fillAll(rowIsSelected ? juce::Colour(0xff2f5d8a) : listBackground);
        g.setColour(juce::Colour(0xff262b31));
        g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));

        // Visibility "eye".
        auto eye = area.removeFromLeft(eyeWidth).toFloat().withSizeKeepingCentre(16.0f, 10.0f);
        g.setColour(layer->visible ? juce::Colours::white : juce::Colours::grey.withAlpha(0.5f));
        g.drawEllipse(eye, 1.4f);
        if (layer->visible)
            g.fillEllipse(eye.withSizeKeepingCentre(5.0f, 5.0f));

        auto thumbArea = area.removeFromLeft(height).reduced(5);
        drawCheckerboard(g, thumbArea, 5);
        const auto thumbnail = document.getLayerThumbnail(index);
        if (thumbnail.isValid())
            g.drawImage(thumbnail, thumbArea.toFloat(), juce::RectanglePlacement::centred);

        auto text = area.reduced(8, 6);
        g.setColour(layer->visible ? juce::Colours::white : juce::Colours::grey);
        g.setFont(juce::FontOptions(14.0f));
        g.drawText(layer->name, text.removeFromTop(text.getHeight() / 2), juce::Justification::bottomLeft, true);
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions(12.0f));
        g.drawText(image_lab::blendModeNames()[static_cast<int>(layer->mode)] + "  "
                       + juce::String(juce::roundToInt(layer->opacity * 100.0f)) + "%",
                   text, juce::Justification::topLeft, true);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        const int index = indexForRow(row);
        const auto* layer = document.getLayer(index);
        if (layer == nullptr)
            return;
        if (e.x < eyeWidth)
            document.setVisible(index, ! layer->visible);
        else
            document.setActiveIndex(index);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (lastRowSelected >= 0)
            document.setActiveIndex(indexForRow(lastRowSelected));
    }

    juce::var getDragSourceDescription(const juce::SparseSet<int>&) override { return layerDragId; }

    ImageLabDocument& document;
    juce::ListBox list;
    int dropRow = -1;
};

// Layers: the active layer's blend mode and opacity at the top (like GIMP's Layers dock), the stack, and the
// layer commands.
class ImageLabWorkspace::LayersPanel final : public juce::Component
{
public:
    LayersPanel(ImageLabWorkspace& w, ImageLabDocument& d) : workspace(w), document(d), layerList(d)
    {
        modeBox.addItemList(image_lab::blendModeNames(), 1);
        modeBox.onChange = [this]() {
            const int id = modeBox.getSelectedId();
            if (id > 0)
                document.setBlendMode(document.getActiveIndex(), static_cast<image_lab::BlendMode>(id - 1));
        };
        modeBox.setTooltip("Blend mode of the active layer");
        addAndMakeVisible(modeBox);

        opacitySlider.setSliderStyle(juce::Slider::LinearBar);
        opacitySlider.setRange(0.0, 100.0, 1.0);
        opacitySlider.setTextValueSuffix("% opacity");
        opacitySlider.onDragStart = [this]() { document.beginEdit("Layer opacity"); };
        opacitySlider.onValueChange = [this]() {
            if (! opacitySlider.isMouseButtonDown())
                document.beginEdit("Layer opacity");
            document.setOpacity(document.getActiveIndex(), static_cast<float>(opacitySlider.getValue() / 100.0));
        };
        addAndMakeVisible(opacitySlider);

        addAndMakeVisible(layerList);

        addButton.onClick = [this]() { workspace.addImageLayer(); };
        duplicateButton.onClick = [this]() { document.duplicateActive(); };
        deleteButton.onClick = [this]() { document.removeActive(); };
        saveButton.onClick = [this]() { workspace.saveImage(); };
        for (auto* button : { &addButton, &duplicateButton, &deleteButton, &saveButton })
            addAndMakeVisible(*button);

        refresh();
    }

    void refresh()
    {
        const auto* layer = document.getLayer(document.getActiveIndex());
        const bool hasLayer = layer != nullptr;
        modeBox.setEnabled(hasLayer);
        opacitySlider.setEnabled(hasLayer);
        duplicateButton.setEnabled(hasLayer);
        deleteButton.setEnabled(hasLayer);
        saveButton.setEnabled(document.getNumLayers() > 0);
        if (hasLayer)
        {
            modeBox.setSelectedId(static_cast<int>(layer->mode) + 1, juce::dontSendNotification);
            if (! opacitySlider.isMouseButtonDown())
                opacitySlider.setValue(layer->opacity * 100.0, juce::dontSendNotification);
        }
        layerList.update();
    }

    void paint(juce::Graphics& g) override { g.fillAll(panelBackground); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        modeBox.setBounds(area.removeFromTop(26));
        area.removeFromTop(6);
        opacitySlider.setBounds(area.removeFromTop(24));
        area.removeFromTop(8);

        auto bottom = area.removeFromBottom(62);
        auto row1 = bottom.removeFromTop(28);
        const int third = row1.getWidth() / 3;
        addButton.setBounds(row1.removeFromLeft(third).reduced(2, 0));
        duplicateButton.setBounds(row1.removeFromLeft(third).reduced(2, 0));
        deleteButton.setBounds(row1.reduced(2, 0));
        bottom.removeFromTop(6);
        saveButton.setBounds(bottom.removeFromTop(28).reduced(2, 0));

        area.removeFromBottom(8);
        layerList.setBounds(area);
    }

private:
    ImageLabWorkspace& workspace;
    ImageLabDocument& document;
    ImageLabLayerList layerList;
    juce::ComboBox modeBox;
    juce::Slider opacitySlider;
    juce::TextButton addButton { "Add Image..." };
    juce::TextButton duplicateButton { "Duplicate" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton saveButton { "Save Image..." };
};

// History: every step, oldest first, with the current position highlighted. Click a step to go back (or forward) to
// just after it; "Start" is before the first step.
class ImageLabWorkspace::HistoryPanel final : public juce::Component,
                                                private juce::ListBoxModel
{
public:
    explicit HistoryPanel(ImageLabDocument& d) : document(d)
    {
        list.setModel(this);
        list.setRowHeight(24);
        list.setColour(juce::ListBox::backgroundColourId, listBackground);
        addAndMakeVisible(list);

        undoButton.onClick = [this]() { document.getUndoManager().undo(); };
        redoButton.onClick = [this]() { document.getUndoManager().redo(); };
        addAndMakeVisible(undoButton);
        addAndMakeVisible(redoButton);
        refresh();
    }

    void refresh()
    {
        auto& undo = document.getUndoManager();
        done = undo.getUndoDescriptions(); // most recent first
        pending = undo.getRedoDescriptions(); // next redo first
        undoButton.setEnabled(undo.canUndo());
        redoButton.setEnabled(undo.canRedo());
        list.updateContent();
        list.selectRow(done.size(), true, true);
        list.repaint();
    }

    void paint(juce::Graphics& g) override { g.fillAll(panelBackground); }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);
        auto buttons = area.removeFromBottom(28);
        undoButton.setBounds(buttons.removeFromLeft(buttons.getWidth() / 2).reduced(2, 0));
        redoButton.setBounds(buttons.reduced(2, 0));
        area.removeFromBottom(6);
        list.setBounds(area);
    }

private:
    // Row 0 is "Start"; rows 1..done.size() are the steps done, oldest first; after that, the steps that can be redone.
    int getNumRows() override { return 1 + done.size() + pending.size(); }

    juce::String rowText(int row) const
    {
        if (row == 0)
            return "Start";
        if (row <= done.size())
            return done[done.size() - row];
        return pending[row - done.size() - 1];
    }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool rowIsSelected) override
    {
        if (rowIsSelected)
            g.fillAll(juce::Colour(0xff2f5d8a));
        const bool undone = row > done.size();
        g.setColour(undone ? juce::Colours::grey : juce::Colours::white);
        g.setFont(juce::FontOptions(13.0f, row == 0 ? juce::Font::italic : juce::Font::plain));
        auto text = rowText(row);
        g.drawText(text.isNotEmpty() ? text : juce::String("Change"), 8, 0, width - 16, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemClicked(int row, const juce::MouseEvent&) override
    {
        auto& undo = document.getUndoManager();
        int current = done.size();
        while (current > row && undo.undo())
            --current;
        while (current < row && undo.redo())
            ++current;
    }

    ImageLabDocument& document;
    juce::ListBox list;
    juce::StringArray done, pending;
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
};

ImageLabWorkspace::ImageLabWorkspace()
    : canvas(std::make_unique<Canvas>(document)),
      layersPanel(std::make_unique<LayersPanel>(*this, document)),
      historyPanel(std::make_unique<HistoryPanel>(document))
{
    document.addChangeListener(this);
    document.getUndoManager().addChangeListener(this);
}

ImageLabWorkspace::~ImageLabWorkspace()
{
    document.getUndoManager().removeChangeListener(this);
    document.removeChangeListener(this);
}

juce::Component& ImageLabWorkspace::getCanvas() noexcept { return *canvas; }
juce::Component& ImageLabWorkspace::getLayersPanel() noexcept { return *layersPanel; }
juce::Component& ImageLabWorkspace::getHistoryPanel() noexcept { return *historyPanel; }

void ImageLabWorkspace::changeListenerCallback(juce::ChangeBroadcaster*)
{
    layersPanel->refresh();
    historyPanel->refresh();
    canvas->repaint();
}

void ImageLabWorkspace::status(const juce::String& text)
{
    if (onStatus)
        onStatus(text);
}

void ImageLabWorkspace::addImageLayer()
{
    // The same image dialog Properties uses: pick from the project's images.
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Add an image layer";
    options.content.setOwned(new ProjectImagePicker(images, {}, [this](const juce::String& path) {
        if (path.isEmpty() || ! images.image)
            return;

        const auto image = images.image(path);
        if (! image.isValid())
        {
            status("Could not read " + path + ".");
            return;
        }

        juce::String name = path.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false);
        if (images.list)
            for (const auto& entry : images.list())
                if (entry.logicalPath == path)
                    name = entry.displayName;

        auto layer = image_lab::layerFromImage(image, name);
        layer.sourceAsset = path;
        const bool first = document.getNumLayers() == 0;
        document.addLayer(std::move(layer));
        if (first)
            canvas->resetView();
        status("Added layer " + name + ".");
    }));
    options.componentToCentreAround = canvas->getTopLevelComponent();
    options.dialogBackgroundColour = listBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void ImageLabWorkspace::saveImage()
{
    if (projectSession == nullptr || ! projectSession->isValid())
    {
        status("No project is open. Open or create a project first.");
        return;
    }

    auto* prompt = new juce::AlertWindow("Save Image", "Save the combined layers as a new image in the project:",
                                         juce::MessageBoxIconType::NoIcon, canvas.get());
    prompt->addTextEditor("name", "Image Lab image");
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt](int result) {
        if (result != 1)
            return;

        const auto name = prompt->getTextEditorContents("name").trim();
        const auto image = document.getComposite();
        if (name.isEmpty() || ! image.isValid())
        {
            status(name.isEmpty() ? "An image needs a name." : "Nothing to save: " + document.getLastError());
            return;
        }

        juce::MemoryOutputStream png;
        juce::PNGImageFormat format;
        if (! format.writeImageToStream(image, png))
        {
            status("Could not encode the image as PNG.");
            return;
        }

        creation::assets::ProjectAssetService::ImportOptions options;
        options.kind = creation::assets::AssetKind::texture;
        options.displayName = name;
        options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(name) + ".png";
        options.mediaType = "image/png";
        options.sourceApp = "Djehuti Texture";
        options.sourceTool = "Image Lab";
        options.description = "Image made in Image Lab from " + juce::String(document.getNumLayers()) + " layer(s).";
        options.tags = { "image-lab" };

        creation::assets::AssetDescriptor saved;
        juce::String error;
        if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, png.getMemoryBlock(), options, saved, error)
            || ! projectSession->commit(error))
        {
            status("Could not save the image: " + error);
            return;
        }
        status("Saved " + name + " to the project.");
    }), true);
}
