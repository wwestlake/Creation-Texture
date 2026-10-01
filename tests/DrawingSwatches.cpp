// Renders a swatch sheet of the Drawing brushes to a PNG, to look at by eye. Developer tool, no UI.
//   DjehutiTextureDrawingSwatches.exe <out.png>
// One row per brush: a line and a circle outline painted with it, plus a filled polygon at the end.

#include "NoCrashDialogs.h"

#include <ImageGraph.h>

#include <iostream>

namespace ns = ce::node_system;

namespace
{
void set(ns::Node* node, const std::string& pin, ns::PinDefaultValue value)
{
    for (const auto& p : node->Inputs())
        if (p.name == pin)
            node->FindPin(p.id)->defaultValue = value;
}

ns::PinId pinId(ns::Node* node, const std::string& name, bool input)
{
    for (const auto& p : input ? node->Inputs() : node->Outputs())
        if (p.name == name)
            return p.id;
    return 0;
}

void connect(ns::Graph& graph, ns::Node* from, const std::string& out, ns::Node* to, const std::string& in)
{
    if (! graph.Connect(from->Id(), pinId(from, out, false), to->Id(), pinId(to, in, true)).has_value())
        std::cerr << "could not connect " << out << " -> " << in << "\n";
}
}

int main(int argc, char** argv)
{
    disableCrashDialogs();
    if (argc < 2)
    {
        std::cerr << "usage: DjehutiTextureDrawingSwatches <out.png>\n";
        return 2;
    }
    image_graph::Library library;
    ns::NodeTypeRegistry registry;
    library.registerTypes(registry);
    image_graph::Evaluator evaluator(library, {});
    if (! evaluator.isReady())
    {
        std::cerr << evaluator.getError() << "\n";
        return 1;
    }
    ns::Graph graph("swatches");
    auto add = [&](const char* type) { return ns::AddRegisteredNode(graph, registry, type); };

    auto* canvas = add("image.create");
    set(canvas, "width", std::int64_t { 1024 });
    set(canvas, "height", std::int64_t { 1024 });
    set(canvas, "color", ns::Vec3Default { 0.08f, 0.08f, 0.1f });
    ns::Node* last = canvas;

    auto paintOn = [&](ns::Node* shape, ns::Node* brush, std::int64_t mode) {
        auto* p = add("draw.paint");
        connect(graph, last, "image", p, "canvas");
        connect(graph, shape, "drawing", p, "drawing");
        connect(graph, brush, "brush", p, "brush");
        set(p, "mode", mode);
        set(p, "fill", ns::Vec3Default { 0.2f, 0.45f, 0.8f });
        last = p;
    };

    auto* tipNoise = add("image.noise");
    set(tipNoise, "width", std::int64_t { 64 });
    set(tipNoise, "height", std::int64_t { 64 });

    struct Row { std::int64_t tip; std::int64_t rotation; float size, hardness, spacing, scatter, sizeJitter, opacityJitter, taper; ns::Vec3Default colour; };
    const Row rows[] = {
        { 0, 0, 0.03f, 0.2f, 0.1f, 0.0f, 0.0f, 0.0f, 0.3f, { 1.0f, 0.9f, 0.7f } },  // round, soft, tapered
        { 1, 1, 0.03f, 1.0f, 0.15f, 0.0f, 0.4f, 0.0f, 0.0f, { 0.9f, 0.3f, 0.2f } }, // square following the stroke, size jitter
        { 2, 0, 0.04f, 0.6f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.95f, 0.95f, 0.95f } }, // chalk
        { 3, 1, 0.05f, 0.7f, 0.05f, 0.0f, 0.0f, 0.0f, 0.15f, { 0.5f, 0.8f, 0.3f } }, // bristle following the stroke
        { 4, 2, 0.06f, 1.0f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, { 0.8f, 0.6f, 1.0f } },  // image tip, random rotation
        { 0, 0, 0.02f, 0.9f, 1.0f, 1.5f, 0.6f, 0.7f, 0.0f, { 1.0f, 0.8f, 0.2f } },  // scattered, jittered dots
    };
    float y = 0.09f;
    for (const auto& row : rows)
    {
        auto* brush = add("draw.brush");
        set(brush, "tip", row.tip);
        set(brush, "rotation", row.rotation);
        set(brush, "size", row.size);
        set(brush, "hardness", row.hardness);
        set(brush, "spacing", row.spacing);
        set(brush, "scatter", row.scatter);
        set(brush, "sizeJitter", row.sizeJitter);
        set(brush, "opacityJitter", row.opacityJitter);
        set(brush, "taperStart", row.taper);
        set(brush, "taperEnd", row.taper);
        set(brush, "color", row.colour);
        if (row.tip == 4)
            connect(graph, tipNoise, "image", brush, "tipImage");

        auto* line = add("draw.line");
        set(line, "x1", 0.05f); set(line, "y1", y + 0.03f); set(line, "x2", 0.55f); set(line, "y2", y - 0.03f);
        paintOn(line, brush, 0);
        auto* circle = add("draw.circle");
        set(circle, "x", 0.7f); set(circle, "y", y); set(circle, "radius", 0.055f);
        paintOn(circle, brush, 0);
        auto* poly = add("draw.polygon");
        set(poly, "x", 0.89f); set(poly, "y", y); set(poly, "radius", 0.055f); set(poly, "sides", std::int64_t { 5 });
        paintOn(poly, brush, 2);
        y += 0.165f;
    }

    juce::String error;
    const auto image = evaluator.evaluate(graph, last->Id(), "image", error);
    if (image == nullptr)
    {
        std::cerr << error << "\n";
        return 1;
    }
    const juce::File out { juce::String(argv[1]) };
    out.deleteFile();
    juce::FileOutputStream stream(out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream(image_graph::toDisplayImage(*image), stream))
    {
        std::cerr << "could not write " << argv[1] << "\n";
        return 1;
    }
    std::cout << "wrote " << argv[1] << "\n";
    return 0;
}
