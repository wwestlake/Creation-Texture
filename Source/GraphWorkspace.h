#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <creation/node_editor_ui/SymbolsPanel.h>
#include <creation/node_editor_ui/TypesPanel.h>
#include <ImageGraph.h>
#include <creation/material/material_nodes.h>
#include "NodePropertiesPanel.h"
#include "DrawPanels.h"
#include "ProjectImagePicker.h"
#include "ViewerPanel.h"

// The Graph editor (Layout > Graph, and its Draw view): one node editor for every kind of graph Texture makes
// (shared/NodeSystem/GRAPH_TYPES.md). The graph's type picks its node list and how its result is made and shown:
//   image    - evaluated to pixels on a background thread; every node shows a thumbnail and the 2D Preview shows the
//              selected (or pinned) node's output. Saves as an image graph document (.imggraph.json). Section 4b.
//   material - compiled to a shader and shown lit on the 3D Preview. Saves as a material asset (.frgraph), which other
//              apps read.
// Both reopen exactly as saved.
class GraphWorkspace final : private juce::ChangeListener
{
public:
    GraphWorkspace();
    ~GraphWorkspace() override;

    void setProjectSession(creation::assets::ProjectSession* session);
    void setImageSource(project_images::Source source);
    std::function<void(const juce::String&)> onStatus;

    juce::Component& getPalette() noexcept { return palette; }
    juce::Component& getGraphView() noexcept { return graphView; }
    juce::Component& getPropertiesPanel() noexcept { return properties; }
    juce::Component& getVariablesPanel() noexcept { return symbols; }
    // The types in scope and their editors (shared/NodeSystem/TYPES.md).
    juce::Component& getTypesPanel() noexcept { return types; }
    // A project was opened (or the session became valid): load the project's types into scope.
    void projectOpened();
    juce::Component& getPreview() noexcept;
    // A material graph's lit 3D preview.
    juce::Component& getMaterialPreview() noexcept { return materialPreview; }
    // The Draw view's own panels (Layout > Draw): the big canvas with the drawing's lines over it, and the script.
    juce::Component& getDrawCanvas() noexcept { return drawCanvas; }
    juce::Component& getScriptPanel() noexcept { return scriptPanel; }

    // The kind of graph being edited (GRAPH_TYPES.md): image_graph::kImageDiagram or ce::material::kMaterialDiagram.
    const std::string& getDiagramType() const noexcept { return graph.DiagramType(); }
    bool isMaterial() const noexcept { return graph.DiagramType() == ce::material::kMaterialDiagram; }
    // The type changed (New, Open): the app shows the preview that type uses.
    std::function<void()> onTypeChanged;
    bool hasUnsavedEdits() const noexcept { return edited; }

    // File menu.
    void newGraph(const std::string& diagramType);
    void openGraph(); // image graphs and materials both
    void saveGraph();
    void saveGraphAs();
    void saveOutputAsImage();
    // Computes every Output node at full size and saves each as a project image named by the node.
    void renderOutputs();
    bool hasProject() const noexcept { return projectSession != nullptr && projectSession->isValid(); }
    bool canSaveOutput() const noexcept;
    juce::String getTitle() const;

    // For the Virtual Engineer (shared/VirtualEngineer): a short description of what is open, sent with every request,
    // and what its API endpoints show - the graph (with its exact saved text), the types in scope, the current errors.
    juce::String describeForAgent() const;
    juce::var graphForAgent() const;
    juce::var typesForAgent() const;
    juce::var errorsForAgent() const;

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
    // Graph nodes (shared/NodeSystem/GRAPH_TYPES.md): choose the graph one uses, and bring their pins up to date.
    void chooseGraphFor(ce::node_system::NodeId node);
    image_graph::Host::LoadedGraph readProjectGraph(const juce::String& path) const;
    void syncGraphNodes();
    juce::String ownGraphPath() const;
    void writeGraph(const juce::String& name);
    void writeMaterial(const juce::String& name);
    // Starts editing a graph (new or opened): its type sets the node list, Variables types and preview.
    void adoptGraph(ce::node_system::Graph newGraph, const juce::String& name);
    void compileMaterial();
    // The project's types: read from and written to the project (TYPES.md), and put in the registry's project scope.
    void loadProjectTypes();
    // The project's structs and enums, for the evaluator (copies: it runs on another thread).
    struct ProjectTypes
    {
        std::vector<ce::node_system::StructDef> structs;
        std::vector<ce::node_system::EnumDef> enums;
    };
    ProjectTypes projectTypes() const;
    void saveProjectTypes(const std::vector<ce::node_system::EnumDef>& enums, const std::vector<ce::node_system::StructDef>& structs);
    void typesChanged();
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
    creation::node_editor_ui::TypesPanel types { graph, registry };
    std::unique_ptr<PreviewPanel> preview;
    ViewerPanel materialPreview;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GraphWorkspace)
};
