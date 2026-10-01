#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <creation/node_editor_ui/SymbolsPanel.h>
#include <ImageGraph.h>
#include "NodePropertiesPanel.h"
#include "DrawPanels.h"
#include "ProjectImagePicker.h"

// Image Graph mode (Layout > Image Graph): make and process images with nodes. Panels: Nodes (palette), Graph,
// Properties, 2D Preview. Every node shows a thumbnail; the preview shows the selected (or pinned) node's chosen
// output. Computing happens on a background thread from a copy of the graph. The graph saves as a JSON document
// that reopens exactly. See docs/REQUIREMENTS.md section 4b.
class ImageGraphWorkspace final : private juce::ChangeListener
{
public:
    ImageGraphWorkspace();
    ~ImageGraphWorkspace() override;

    void setProjectSession(creation::assets::ProjectSession* session);
    void setImageSource(project_images::Source source);
    std::function<void(const juce::String&)> onStatus;

    juce::Component& getPalette() noexcept { return palette; }
    juce::Component& getGraphView() noexcept { return graphView; }
    juce::Component& getPropertiesPanel() noexcept { return properties; }
    juce::Component& getVariablesPanel() noexcept { return symbols; }
    juce::Component& getPreview() noexcept;
    // The Draw view's own panels (Layout > Draw): the big canvas with the drawing's lines over it, and the script.
    juce::Component& getDrawCanvas() noexcept { return drawCanvas; }
    juce::Component& getScriptPanel() noexcept { return scriptPanel; }

    // File menu.
    void newGraph();
    void openGraph();
    void saveGraph();
    void saveGraphAs();
    void saveOutputAsImage();
    // Computes every Output node at full size and saves each as a project image named by the node.
    void renderOutputs();
    bool hasProject() const noexcept { return projectSession != nullptr && projectSession->isValid(); }
    bool canSaveOutput() const noexcept;
    juce::String getTitle() const;

private:
    class Worker;
    class PreviewPanel;
    class RenderJob;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void graphEdited();
    void requestEvaluation();
    void selectionChanged(ce::node_system::NodeId id);
    void forgetDrawTargets();
    std::string scriptText(ce::node_system::NodeId id) const;
    std::unique_ptr<juce::Component> customEditor(ce::node_system::Node& node, const ce::node_system::Pin& pin, int& height);
    void chooseSurfaceMapFor(ce::node_system::NodeId node);
    void writeGraph(const juce::String& name);
    void status(const juce::String& text);

    creation::assets::ProjectSession* projectSession = nullptr;
    project_images::Source images;

    image_graph::Library library;
    ce::node_system::NodeTypeRegistry registry;
    ce::node_system::Graph graph { "Image Graph", ce::node_system::GraphTarget::Dataflow };

    creation::node_editor_ui::NodeGraphComponent graphView { graph, registry };
    creation::node_editor_ui::NodePalette palette;
    NodePropertiesPanel properties;
    creation::node_editor_ui::SymbolsPanel symbols { graph };
    std::unique_ptr<PreviewPanel> preview;
    DrawCanvas drawCanvas;
    ScriptPanel scriptPanel;
    ce::node_system::NodeId overlayNode = 0; // the drawing shown over the Draw canvas
    ce::node_system::NodeId scriptNode = 0;  // the Draw Script node in the Script panel
    juce::String overlayLabel;
    std::unique_ptr<Worker> worker;

    ce::node_system::NodeId selectedNode = 0;
    std::map<ce::node_system::NodeId, juce::Image> thumbnails;
    std::map<ce::node_system::NodeId, juce::String> errors;
    image_graph::ImagePtr lastPreview;   // the previewed output, for Save Output as Image

    juce::String graphName;
    bool edited = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImageGraphWorkspace)
};
