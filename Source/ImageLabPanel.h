#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include "ImageLabDocument.h"
#include "ProjectImagePicker.h"

// The Image Lab work area: one document and its commands, shown through three dockable panels - the Canvas (the
// composite), Layers (GIMP-style stack with the active layer's mode and opacity), and History (the undo list).
// See docs/REQUIREMENTS.md sections 4-8.
class ImageLabWorkspace final : private juce::ChangeListener
{
public:
    ImageLabWorkspace();
    ~ImageLabWorkspace() override;

    void setProjectSession(creation::assets::ProjectSession* session) { projectSession = session; }
    void setImageSource(project_images::Source source) { images = std::move(source); }
    std::function<void(const juce::String&)> onStatus;

    ImageLabDocument& getDocument() noexcept { return document; }
    juce::Component& getCanvas() noexcept;
    juce::Component& getLayersPanel() noexcept;
    juce::Component& getHistoryPanel() noexcept;

    // Commands, shared by the panels and the Layer menu.
    void addImageLayer();
    void saveImage();
    void undo() { document.getUndoManager().undo(); }
    void redo() { document.getUndoManager().redo(); }

private:
    class Canvas;
    class LayersPanel;
    class HistoryPanel;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void status(const juce::String& text);

    ImageLabDocument document;
    creation::assets::ProjectSession* projectSession = nullptr;
    project_images::Source images;

    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<LayersPanel> layersPanel;
    std::unique_ptr<HistoryPanel> historyPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageLabWorkspace)
};
