#pragma once

#include <JuceHeader.h>
#include <node_system/graph.h>
#include <node_system/type_registry.h>
#include "ProjectImagePicker.h"

// Shows the selected node and lets every value that can be changed right now be edited: each input that is not
// wired gets an editor for its type (number, colour, vector, toggle, text, project image). Built generically from
// the node, so every node type gets it. Selection drives it - no double-click. See docs/REQUIREMENTS.md section 2.
class NodePropertiesPanel final : public juce::Component
{
public:
    struct Host
    {
        ce::node_system::Graph* graph = nullptr;
        const ce::node_system::NodeTypeRegistry* registry = nullptr;
        project_images::Source projectImages;
        // A value was changed in the panel: the graph needs repainting, recompiling, and marking edited.
        std::function<void()> onValueEdited;
        // Optional: a layout's own editor for a particular unwired input (null = the standard editor for its type).
        // The editor writes the pin itself and then calls onValueEdited. `height` is set to the row height wanted.
        std::function<std::unique_ptr<juce::Component>(ce::node_system::Node&, const ce::node_system::Pin&, int& height)> customEditor;
    };

    NodePropertiesPanel();
    ~NodePropertiesPanel() override;

    void setHost(Host newHost);
    void showNode(ce::node_system::NodeId id);
    // Rebuilds for the current node - after wiring changes, a load, or the node being deleted.
    void refresh();

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct Row
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> editor;
        int height = 28;
    };

    void rebuild();
    void addRow(const juce::String& labelText, std::unique_ptr<juce::Component> editor, int height);
    void addInputRow(ce::node_system::Node& node, const ce::node_system::Pin& pin);
    void layoutRows();
    void valueEdited();
    juce::String describeConnection(const ce::node_system::Node& node, const ce::node_system::Pin& pin) const;

    Host host;
    ce::node_system::NodeId nodeId = 0;

    juce::Label title;
    juce::Label subtitle;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NodePropertiesPanel)
};
