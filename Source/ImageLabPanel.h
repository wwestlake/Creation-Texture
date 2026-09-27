#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include "ImageLabDocument.h"
#include "ProjectImagePicker.h"

// The Image Lab work area: the canvas showing the composite, and a GIMP-style Layers panel (mode and opacity of
// the active layer at the top, the stack with visibility/thumbnail/name, drag to reorder, and the layer commands).
// See docs/REQUIREMENTS.md sections 5-8.
class ImageLabPanel final : public juce::Component,
                            private juce::ChangeListener
{
public:
    ImageLabPanel();
    ~ImageLabPanel() override;

    void setProjectSession(creation::assets::ProjectSession* session) { projectSession = session; }
    void setImageSource(project_images::Source source) { images = std::move(source); }
    std::function<void(const juce::String&)> onStatus;

    ImageLabDocument& getDocument() noexcept { return document; }
    void addImageLayer();
    void saveImage();

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    class Canvas;
    class LayerList;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshControls();
    void status(const juce::String& text);

    ImageLabDocument document;
    creation::assets::ProjectSession* projectSession = nullptr;
    project_images::Source images;

    std::unique_ptr<Canvas> canvas;
    std::unique_ptr<LayerList> layerList;

    juce::Label layersTitle;
    juce::ComboBox modeBox;
    juce::Slider opacitySlider;
    juce::TextButton addButton { "Add Image..." };
    juce::TextButton duplicateButton { "Duplicate" };
    juce::TextButton deleteButton { "Delete" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };
    juce::TextButton saveButton { "Save Image..." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageLabPanel)
};
