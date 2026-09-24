#pragma once

#include <JuceHeader.h>
#include <node_system/graph.h>
#include <node_system/node_library.h>
#include <creation/node_editor_ui/NodeGraphComponent.h>
#include <creation/node_editor_ui/NodePalette.h>
#include <creation/ui/FrustyComponent.h>
#include "ViewerNodeEditor.h"

class ProceduralWorkspacePanel : public juce::Component,
                                 public juce::FileDragAndDropTarget
{
public:
    ProceduralWorkspacePanel();
    void setSummaryText(const juce::String& text) {}
    ~ProceduralWorkspacePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragMove(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds);

    ce::node_system::NodeLibraryRegistry registry;
    ce::node_system::Graph graph;

    creation::node_editor_ui::NodeGraphComponent graphComponent;
    creation::node_editor_ui::NodePalette paletteComponent;
    creation::ui::FrustyComponent frusty;
    
    juce::TextButton compileButton{"Compile Graph"};
    std::shared_ptr<TextureFrameSnapshot> currentSnapshot;
    juce::Array<juce::Component::SafePointer<ViewerNodeEditor>> activeViewers;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ProceduralWorkspacePanel)
};
