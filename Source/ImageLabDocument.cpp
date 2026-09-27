#include "ImageLabDocument.h"

namespace
{
class InsertLayerAction final : public juce::UndoableAction
{
public:
    InsertLayerAction(ImageLabDocument& d, int i, ImageLabDocument::LayerPtr l) : document(d), index(i), layer(std::move(l)) {}
    bool perform() override { document.insertLayerAt(index, layer); return true; }
    bool undo() override { document.removeLayerAt(index); return true; }
    int getSizeInUnits() override { return 1; }

private:
    ImageLabDocument& document;
    int index;
    ImageLabDocument::LayerPtr layer;
};

class RemoveLayerAction final : public juce::UndoableAction
{
public:
    RemoveLayerAction(ImageLabDocument& d, int i) : document(d), index(i) {}
    bool perform() override { layer = document.removeLayerAt(index); return layer != nullptr; }
    bool undo() override { document.insertLayerAt(index, layer); return true; }
    int getSizeInUnits() override { return 1; }

private:
    ImageLabDocument& document;
    int index;
    ImageLabDocument::LayerPtr layer;
};

class MoveLayerAction final : public juce::UndoableAction
{
public:
    MoveLayerAction(ImageLabDocument& d, int f, int t) : document(d), from(f), to(t) {}
    bool perform() override { document.moveLayerRaw(from, to); return true; }
    bool undo() override { document.moveLayerRaw(to, from); return true; }
    int getSizeInUnits() override { return 1; }

private:
    ImageLabDocument& document;
    int from, to;
};

// Visibility, opacity, or blend mode of one layer.
class LayerSettingsAction final : public juce::UndoableAction
{
public:
    struct Settings
    {
        bool visible = true;
        float opacity = 1.0f;
        image_lab::BlendMode mode = image_lab::BlendMode::normal;
    };

    LayerSettingsAction(ImageLabDocument& d, ImageLabDocument::LayerPtr l, Settings before, Settings after, bool isOpacity)
        : document(d), layer(std::move(l)), oldSettings(before), newSettings(after), opacityChange(isOpacity) {}

    bool perform() override { apply(newSettings); return true; }
    bool undo() override { apply(oldSettings); return true; }
    int getSizeInUnits() override { return 1; }

    // Dragging the opacity slider makes many small changes; they merge into one undo step.
    juce::UndoableAction* createCoalescedAction(juce::UndoableAction* next) override
    {
        if (auto* other = dynamic_cast<LayerSettingsAction*>(next))
            if (opacityChange && other->opacityChange && other->layer == layer)
                return new LayerSettingsAction(document, layer, oldSettings, other->newSettings, true);
        return nullptr;
    }

private:
    void apply(const Settings& settings)
    {
        layer->visible = settings.visible;
        layer->opacity = settings.opacity;
        layer->mode = settings.mode;
        document.layerChanged();
    }

    ImageLabDocument& document;
    ImageLabDocument::LayerPtr layer;
    Settings oldSettings, newSettings;
    bool opacityChange;
};

LayerSettingsAction::Settings settingsOf(const image_lab::Layer& layer)
{
    return { layer.visible, layer.opacity, layer.mode };
}
}

ImageLabDocument::ImageLabDocument() = default;

const image_lab::Layer* ImageLabDocument::getLayer(int index) const
{
    return juce::isPositiveAndBelow(index, getNumLayers()) ? layers[static_cast<size_t>(index)].get() : nullptr;
}

ImageLabDocument::LayerPtr ImageLabDocument::getLayerPtr(int index) const
{
    return juce::isPositiveAndBelow(index, getNumLayers()) ? layers[static_cast<size_t>(index)] : nullptr;
}

void ImageLabDocument::setActiveIndex(int index)
{
    const int clamped = layers.empty() ? -1 : juce::jlimit(0, getNumLayers() - 1, index);
    if (clamped == activeIndex)
        return;
    activeIndex = clamped;
    sendChangeMessage();
}

void ImageLabDocument::addLayer(image_lab::Layer layer)
{
    if (! layer.isValid())
        return;

    const int index = layers.empty() ? 0 : activeIndex + 1;
    undoManager.beginNewTransaction("Add layer");
    undoManager.perform(new InsertLayerAction(*this, index, std::make_shared<image_lab::Layer>(std::move(layer))));
}

void ImageLabDocument::duplicateActive()
{
    auto source = getLayerPtr(activeIndex);
    if (source == nullptr)
        return;

    auto copy = std::make_shared<image_lab::Layer>(*source);
    copy->name = source->name + " copy";
    undoManager.beginNewTransaction("Duplicate layer");
    undoManager.perform(new InsertLayerAction(*this, activeIndex + 1, std::move(copy)));
}

void ImageLabDocument::removeActive()
{
    if (getLayerPtr(activeIndex) == nullptr)
        return;
    undoManager.beginNewTransaction("Delete layer");
    undoManager.perform(new RemoveLayerAction(*this, activeIndex));
}

void ImageLabDocument::moveLayer(int fromIndex, int toIndex)
{
    toIndex = juce::jlimit(0, getNumLayers() - 1, toIndex);
    if (! juce::isPositiveAndBelow(fromIndex, getNumLayers()) || fromIndex == toIndex)
        return;
    undoManager.beginNewTransaction("Move layer");
    undoManager.perform(new MoveLayerAction(*this, fromIndex, toIndex));
}

void ImageLabDocument::setVisible(int index, bool visible)
{
    auto layer = getLayerPtr(index);
    if (layer == nullptr || layer->visible == visible)
        return;
    auto after = settingsOf(*layer);
    after.visible = visible;
    undoManager.beginNewTransaction(visible ? "Show layer" : "Hide layer");
    undoManager.perform(new LayerSettingsAction(*this, layer, settingsOf(*layer), after, false));
}

void ImageLabDocument::setOpacity(int index, float opacity)
{
    auto layer = getLayerPtr(index);
    opacity = juce::jlimit(0.0f, 1.0f, opacity);
    if (layer == nullptr || juce::approximatelyEqual(layer->opacity, opacity))
        return;
    auto after = settingsOf(*layer);
    after.opacity = opacity;
    undoManager.perform(new LayerSettingsAction(*this, layer, settingsOf(*layer), after, true));
}

void ImageLabDocument::setBlendMode(int index, image_lab::BlendMode mode)
{
    auto layer = getLayerPtr(index);
    if (layer == nullptr || layer->mode == mode)
        return;
    auto after = settingsOf(*layer);
    after.mode = mode;
    undoManager.beginNewTransaction("Layer mode");
    undoManager.perform(new LayerSettingsAction(*this, layer, settingsOf(*layer), after, false));
}

void ImageLabDocument::insertLayerAt(int index, LayerPtr layer)
{
    index = juce::jlimit(0, getNumLayers(), index);
    if (layers.empty())
    {
        width = layer->width;
        height = layer->height;
    }
    layers.insert(layers.begin() + index, std::move(layer));
    activeIndex = index;
    stackChanged();
}

ImageLabDocument::LayerPtr ImageLabDocument::removeLayerAt(int index)
{
    if (! juce::isPositiveAndBelow(index, getNumLayers()))
        return nullptr;

    auto layer = layers[static_cast<size_t>(index)];
    thumbnails.erase(layer.get());
    layers.erase(layers.begin() + index);
    if (layers.empty())
    {
        width = height = 0;
        activeIndex = -1;
    }
    else
    {
        activeIndex = juce::jlimit(0, getNumLayers() - 1, index - 1 < 0 ? 0 : index - 1);
    }
    stackChanged();
    return layer;
}

void ImageLabDocument::moveLayerRaw(int fromIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow(fromIndex, getNumLayers()) || ! juce::isPositiveAndBelow(toIndex, getNumLayers()))
        return;
    auto layer = layers[static_cast<size_t>(fromIndex)];
    layers.erase(layers.begin() + fromIndex);
    layers.insert(layers.begin() + toIndex, std::move(layer));
    activeIndex = toIndex;
    stackChanged();
}

void ImageLabDocument::layerChanged()
{
    compositeValid = false;
    sendChangeMessage();
}

void ImageLabDocument::stackChanged()
{
    compositeValid = false;
    sendChangeMessage();
}

juce::Image ImageLabDocument::getComposite()
{
    if (compositeValid)
        return composite;

    compositeValid = true;
    composite = {};
    lastError.clear();
    if (layers.empty())
        return composite;

    std::vector<const image_lab::Layer*> bottomToTop;
    for (const auto& layer : layers)
        bottomToTop.push_back(layer.get());

    std::vector<float> pixels;
    if (! image_lab::flatten(bottomToTop, width, height, compositor, pixels, lastError))
        return composite;

    composite = image_lab::imageFromPixels(pixels, width, height);
    return composite;
}

juce::Image ImageLabDocument::getLayerThumbnail(int index)
{
    const auto* layer = getLayer(index);
    if (layer == nullptr)
        return {};

    auto found = thumbnails.find(layer);
    if (found != thumbnails.end())
        return found->second;

    const auto full = image_lab::imageFromPixels(layer->pixels, layer->width, layer->height);
    juce::Image thumbnail;
    if (full.isValid())
    {
        const float scale = juce::jmin(1.0f, 64.0f / static_cast<float>(juce::jmax(full.getWidth(), full.getHeight())));
        thumbnail = full.rescaled(juce::jmax(1, juce::roundToInt(static_cast<float>(full.getWidth()) * scale)),
                                  juce::jmax(1, juce::roundToInt(static_cast<float>(full.getHeight()) * scale)),
                                  juce::Graphics::mediumResamplingQuality);
    }
    thumbnails[layer] = thumbnail;
    return thumbnail;
}
