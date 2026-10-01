#pragma once

#include <juce_graphics/juce_graphics.h>
#include <node_system/graph.h>
#include <node_system/type_registry.h>
#include <node_system/symbol_nodes.h>
#include <node_system/graph_nodes.h>

#include "SurfaceMaps.h"
#include "Drawing.h"

#include <functional>
#include <map>
#include <memory>

// Image Graph (docs/REQUIREMENTS.md section 4b): images made and processed by nodes. No UI.
//
// A node type is a Definition - its descriptor (name, category, pins) and an evaluate function that does the pixel
// work through FRust. Definitions are kept apart from the graph engine so they can move to research's general node
// system later. Image wires use the node system's Texture data type; a node's settings are its unwired input pins.
//
// The Evaluator computes on demand: asking for one output computes only what it depends on, and each node's
// result is cached under a signature of its type, settings and inputs, so changing a setting recomputes only what
// is downstream. A node computes only the outputs somebody asked for.
namespace image_graph
{
struct Image
{
    int width = 0;
    int height = 0;
    std::vector<float> rgba; // linear light, straight alpha
    bool data = false;       // values are data (normal, height, roughness...), not colour: shown without sRGB
};
using ImagePtr = std::shared_ptr<const Image>;

// What the evaluator needs from the app.
struct Host
{
    // A project image by logical path, as a linear-light image (null if it cannot be read).
    std::function<ImagePtr(const juce::String& logicalPath)> loadImage;
    // Values for the graph's params, by symbol id, set from outside (an Automation, the LLM). They win over the
    // param's own default (shared/NodeSystem/SYMBOLS.md).
    std::map<std::string, ce::node_system::PinDefaultValue> paramOverrides;

    // A saved image graph by project path, for Graph nodes (shared/NodeSystem/GRAPH_TYPES.md): the graph, and its
    // saved text so a change to it is noticed. A null graph if it cannot be read.
    struct LoadedGraph
    {
        std::shared_ptr<const ce::node_system::Graph> graph;
        std::string text;
        juce::String error;
    };
    std::function<LoadedGraph(const juce::String& logicalPath)> loadGraph;

    // Set when this evaluator runs a graph that a Graph node uses: the images wired into the Graph node, by the name of
    // the Graph Input node they feed, and keys that change when those images do.
    std::map<std::string, ImagePtr> graphInputs;
    std::map<std::string, std::string> graphInputKeys;
    int depth = 0; // graphs used inside graphs, to stop a graph that uses itself
};

// An image graph document (.imggraph.json): its format name, and reading one into a graph typed as an image graph.
inline constexpr const char* kGraphDocumentFormat = "djehuti-image-graph";
Host::LoadedGraph readGraphDocument(const juce::String& json);

class Routines; // the compiled FRust routines

struct Context
{
    const ce::node_system::Node& node;
    const Host& host;
    Routines& routines;
    surface_maps::Engine& surfaceMaps;
    std::map<std::string, ImagePtr> inputs;     // wired image inputs, by pin name (missing = not wired)
    std::vector<std::string> wantedOutputs;     // the outputs somebody asked for
    // Settings wired from another node (a Get node, a Value node...): they replace the typed-in value.
    std::map<std::string, ce::node_system::PinDefaultValue> wiredValues;
    // Non-image outputs this node produces (Value nodes).
    std::map<std::string, ce::node_system::PinDefaultValue> valueOutputs;
    // Wired Drawing and Brush inputs, by pin name, and the ones this node produces (section 5: Drawing).
    std::map<std::string, drawing::DrawingPtr> drawings;
    std::map<std::string, drawing::BrushPtr> brushes;
    std::map<std::string, drawing::DrawingPtr> drawingOutputs;
    std::map<std::string, drawing::BrushPtr> brushOutputs;
    // The graph being evaluated, for nodes that read its Variables (Draw Script).
    const ce::node_system::Graph* graph = nullptr;

    // The graph's number, integer and toggle Variables by id, with params' outside values applied (toggles are 0 / 1).
    std::map<std::string, double> numericVariables() const;

    // A setting: its wired value if one is wired in, else the value typed into the node.
    const ce::node_system::PinDefaultValue* setting(const std::string& pin) const;

    float number(const std::string& pin, float fallback) const;
    int integer(const std::string& pin, int fallback) const;
    bool flag(const std::string& pin, bool fallback) const;
    // A colour setting as full-precision floats (juce::Colour would round it to 8 bits).
    ce::node_system::Vec3Default colour(const std::string& pin, ce::node_system::Vec3Default fallback) const;
    juce::String text(const std::string& pin) const;
    bool wants(const std::string& output) const;
};

struct Definition
{
    ce::node_system::NodeTypeDescriptor descriptor;
    std::function<bool(Context&, std::map<std::string, ImagePtr>& outputs, juce::String& error)> evaluate;
    // Reads the graph's Variables, so its cached result depends on them too.
    bool readsVariables = false;
    // Reads the images a Graph node passes in (Graph Input), so its cached result depends on them too.
    bool readsGraphInputs = false;
};

// The Image Graph's graph type (shared/NodeSystem/GRAPH_TYPES.md): every node in this library belongs in it.
inline constexpr const char* kImageDiagram = "image";

class Library final
{
public:
    Library();
    void registerTypes(ce::node_system::NodeTypeRegistry& registry) const;
    const Definition* find(const std::string& typeName) const;
    const std::vector<Definition>& all() const noexcept { return definitions; }
    // The named choices the nodes' integer settings use (shared/NodeSystem/enums.h).
    const std::vector<ce::node_system::EnumDef>& getEnums() const noexcept { return enums; }

private:
    std::vector<Definition> definitions;
    std::vector<ce::node_system::EnumDef> enums;
};

class Evaluator final
{
public:
    Evaluator(const Library& library, Host host);
    ~Evaluator();
    Evaluator(const Evaluator&) = delete;
    Evaluator& operator=(const Evaluator&) = delete;

    bool isReady() const noexcept;
    juce::String getError() const;

    // The image on one output of one node, computing whatever it depends on. Null with an error if it cannot.
    ImagePtr evaluate(const ce::node_system::Graph& graph, ce::node_system::NodeId node, const std::string& output, juce::String& error);
    // A non-image output (a Get node's or a Value node's value).
    bool evaluateValue(const ce::node_system::Graph& graph, ce::node_system::NodeId node, const std::string& output,
                       ce::node_system::PinDefaultValue& value, juce::String& error);
    // A Drawing output (what a shape or drawing node makes), for showing it over the canvas.
    drawing::DrawingPtr evaluateDrawing(const ce::node_system::Graph& graph, ce::node_system::NodeId node, const std::string& output,
                                        juce::String& error);
    Host& getHost() noexcept { return host; }

    // Forget cached results (for example after the project's images change).
    void clearCache();

private:
    struct Cached
    {
        std::string signature;
        std::map<std::string, ImagePtr> outputs;
        std::map<std::string, ce::node_system::PinDefaultValue> values;
        std::map<std::string, drawing::DrawingPtr> drawings;
        std::map<std::string, drawing::BrushPtr> brushes;
    };

    bool evaluateNode(const ce::node_system::Graph& graph, ce::node_system::NodeId node, const std::vector<std::string>& wanted,
                      std::string& signatureOut, juce::String& error, int depth);
    // A Graph node: runs the graph it uses, with its params and Graph Inputs set from the node's inputs.
    bool evaluateGraphNode(const ce::node_system::Node& node, const Host::LoadedGraph& used, Context& context,
                           std::map<std::string, ImagePtr>& outputs, juce::String& error);

    // An evaluator for a graph used by a Graph node, sharing the compiled FRust (compiling it again would take seconds).
    Evaluator(const Library& library, Host host, std::shared_ptr<Routines> routines, std::shared_ptr<surface_maps::Engine> surfaceMaps);

    struct Used
    {
        std::string text;
        std::unique_ptr<Evaluator> evaluator;
    };

    const Library& library;
    Host host;
    std::shared_ptr<Routines> routines;
    std::shared_ptr<surface_maps::Engine> surfaceMaps;
    std::map<ce::node_system::NodeId, Cached> cache;
    std::map<std::string, Used> used; // by path
};

// Display helpers.
juce::Image toDisplayImage(const Image& image); // linear -> sRGB juce::Image
ImagePtr fromDisplayImage(const juce::Image& image);
}
