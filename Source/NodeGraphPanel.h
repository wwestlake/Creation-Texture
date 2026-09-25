#pragma once

#include <JuceHeader.h>
#include <node_system/graph.h>
#include <node_system/type_registry.h>
#include <creation/material/material_nodes.h>
#include <creation/material/material_compiler.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <creation/assets/ProjectSession.h>
#include "ViewerNodeEditor.h"

class NodeGraphPanel : public juce::Component,
                                 public juce::FileDragAndDropTarget
{
public:
    void addViewer(ViewerNodeEditor* v);

    NodeGraphPanel();
    ~NodeGraphPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // The graph as .frgraph text, and back. loadGraphText leaves the current graph untouched on failure.
    juce::String getGraphText() const;
    bool loadGraphText(const juce::String& frgraphText, juce::String& errorMessage);
    void clearGraph();

    void setProjectSession(creation::assets::ProjectSession* session) { projectSession = session; }
    std::function<void()> onSaveRequested;
    std::function<void()> onGraphEdited;
    void compileGraph();
private:
    void handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds);
    static bool isImageAsset(const creation::assets::AssetDescriptor& asset);
    juce::Image getProjectImage(const juce::String& logicalPath);

    ce::node_system::NodeTypeRegistry registry;
    ce::node_system::Graph graph;

    creation::node_editor_ui::NodeGraphComponent graphComponent;
    creation::node_editor_ui::NodePalette paletteComponent;
    std::shared_ptr<TextureFrameSnapshot> currentSnapshot;
    juce::Array<juce::Component::SafePointer<ViewerNodeEditor>> activeViewers;
    creation::assets::ProjectSession* projectSession = nullptr;
    std::map<std::string, juce::Image> imagePreviewCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeGraphPanel)
};



