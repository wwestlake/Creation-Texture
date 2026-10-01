#pragma once

#include <JuceHeader.h>
#include <ImageLabCore.h>

#include <map>
#include <memory>
#include <vector>

// The layer stack being edited in Image Lab: its layers (index 0 = bottom), the active layer, the canvas size, and
// undo for every change. Layers are shared_ptrs so undo steps hold them without copying pixels.
// See docs/REQUIREMENTS.md sections 5-8.
class ImageLabDocument final : public juce::ChangeBroadcaster
{
public:
    using LayerPtr = std::shared_ptr<image_lab::Layer>;

    ImageLabDocument();

    int getNumLayers() const noexcept { return static_cast<int>(layers.size()); }
    const image_lab::Layer* getLayer(int index) const;
    int getActiveIndex() const noexcept { return activeIndex; }
    void setActiveIndex(int index);

    int getWidth() const noexcept { return width; }
    int getHeight() const noexcept { return height; }

    // Each of these is one undo step.
    void addLayer(image_lab::Layer layer);          // above the active layer; the first layer sets the canvas size
    void duplicateActive();
    void removeActive();
    void moveLayer(int fromIndex, int toIndex);
    void setVisible(int index, bool visible);
    // Consecutive changes to one layer merge into the current undo step: call beginEdit() when a drag starts.
    void setOpacity(int index, float opacity);
    void beginEdit(const juce::String& name) { undoManager.beginNewTransaction(name); }
    void setBlendMode(int index, image_lab::BlendMode mode);

    juce::UndoManager& getUndoManager() noexcept { return undoManager; }

    // The composite of the visible layers, as an sRGB image for display and saving; empty with no layers.
    juce::Image getComposite();
    juce::String getLastError() const { return lastError; }
    juce::Image getLayerThumbnail(int index);

    // Used by the undo steps.
    void insertLayerAt(int index, LayerPtr layer);
    LayerPtr removeLayerAt(int index);
    void moveLayerRaw(int fromIndex, int toIndex);
    LayerPtr getLayerPtr(int index) const;
    void layerChanged();

private:
    void stackChanged();

    std::vector<LayerPtr> layers;
    int activeIndex = -1;
    int width = 0;
    int height = 0;

    juce::UndoManager undoManager { 0, 100 };
    image_lab::Compositor compositor;

    bool compositeValid = false;
    juce::Image composite;
    juce::String lastError;
    std::map<const image_lab::Layer*, juce::Image> thumbnails;
};
