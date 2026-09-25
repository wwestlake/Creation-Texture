#pragma once

#include <JuceHeader.h>
#include <node_system/graph.h>
#include <node_system/type_registry.h>
#include <creation/material/material_nodes.h>
#include <creation/material/material_compiler.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <creation/ui/FrustyComponent.h>
#include <creation/assets/ProjectSession.h>
#include "ViewerNodeEditor.h"

class NodeGraphPanel : public juce::Component,
                                 public juce::FileDragAndDropTarget
{
public:
    void addViewer(ViewerNodeEditor* v);

    NodeGraphPanel();
    void setSummaryText(const juce::String& text) {}
    ~NodeGraphPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    void saveGraph(const juce::File& file);
    void loadGraph(const juce::File& file);
    void setProjectSession(creation::assets::ProjectSession* session) { projectSession = session; }
    std::function<bool(juce::String&)> onEnsureProjectSessionActive;
    std::function<void(const juce::String&)> onSaveRequested;
    void loadGraphFromJson(const juce::String& json);
    void compileGraph();
private:
    void handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds);

    ce::node_system::NodeTypeRegistry registry;
    ce::node_system::Graph graph;

    creation::node_editor_ui::NodeGraphComponent graphComponent;
    creation::node_editor_ui::NodePalette paletteComponent;
    creation::ui::FrustyComponent frusty;
    
    juce::TextButton compileButton{"Compile Graph"};
    juce::TextButton saveButton{"Save to Project"};
    std::shared_ptr<TextureFrameSnapshot> currentSnapshot;
    juce::Array<juce::Component::SafePointer<ViewerNodeEditor>> activeViewers;
    creation::assets::ProjectSession* projectSession = nullptr;
    std::map<std::string, juce::Image> imagePreviewCache;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodeGraphPanel)
};



