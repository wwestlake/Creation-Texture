// Renders a swatch sheet of the Drawing brushes to a PNG, to look at by eye. Developer tool, no UI.
//   DjehutiTextureDrawingSwatches.exe <out.png> [modifiers | script]
// Brushes: one row per brush - a line and a circle outline painted with it, plus a filled polygon at the end.
// "modifiers": drawings built with Scatter, Repeat, Wobble, Mirror and Jitter. "script": drawings from Draw Script.

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
        std::cerr << "usage: DjehutiTextureDrawingSwatches <out.png> [modifiers | script]\n";
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

    const bool modifiersSheet = argc > 2 && juce::String(argv[2]) == "modifiers";
    auto node = [&](const char* type, std::initializer_list<std::pair<const char*, ns::PinDefaultValue>> values) {
        auto* n = add(type);
        for (const auto& [pin, value] : values)
            set(n, pin, value);
        return n;
    };
    auto modify = [&](const char* type, ns::Node* input, std::initializer_list<std::pair<const char*, ns::PinDefaultValue>> values) {
        auto* n = node(type, values);
        connect(graph, input, "drawing", n, "drawing");
        return n;
    };
    const bool scriptSheet = argc > 2 && juce::String(argv[2]) == "script";
    if (scriptSheet)
    {
        // A fractal tree, a rosette of arcs and a field of stars, all from Draw Script.
        auto* tree = node("draw.script", { { "script", std::string(R"(def branch len depth {
  forward len
  if depth > 0 {
    push; turn -24 + random(-6, 6); branch len * 0.72, depth - 1; pop
    push; turn 22 + random(-6, 6); branch len * 0.68, depth - 1; pop
  }
}
move 0.3 0.97
heading 270
branch 0.2 10)") } });
        paintOn(tree, node("draw.brush", { { "size", 0.004f }, { "hardness", 0.8f }, { "color", ns::Vec3Default { 0.85f, 0.7f, 0.5f } } }), 0);

        auto* rosette = node("draw.script", { { "script", std::string(R"(repeat 18 {
  move 0.75 0.3
  heading i * 20
  arc 0.08 120
  arc 0.08 120
})") } });
        paintOn(rosette, node("draw.brush", { { "size", 0.003f }, { "color", ns::Vec3Default { 0.4f, 0.8f, 1.0f } } }), 0);

        auto* stars = node("draw.script", { { "script", std::string(R"(repeat 60 {
  star random(0.55, 0.98), random(0.55, 0.98), random(0.01, 0.025), random(0.004, 0.01), 5, random(0, 72)
})") } });
        paintOn(stars, node("draw.brush", { { "size", 0.002f }, { "color", ns::Vec3Default { 1.0f, 0.9f, 0.4f } } }), 2);
    }
    else if (modifiersSheet)
    {
        // Grass: one curved blade scattered 400 times over the bottom, painted with a tapered bristle brush.
        auto* blade = node("draw.bezier", { { "x1", 0.5f }, { "y1", 0.5f }, { "cx1", 0.5f }, { "cy1", 0.45f }, { "cx2", 0.51f },
                                            { "cy2", 0.42f }, { "x2", 0.53f }, { "y2", 0.39f } });
        auto* grass = modify("draw.scatter", blade, { { "count", std::int64_t { 400 } }, { "x", 0.0f }, { "y", 0.78f }, { "width", 1.0f },
                                                     { "height", 0.2f }, { "maxRotation", 20.0f }, { "scaleMin", 0.6f }, { "scaleMax", 1.4f } });
        auto* grassBrush = node("draw.brush", { { "size", 0.008f }, { "hardness", 0.7f }, { "color", ns::Vec3Default { 0.25f, 0.6f, 0.2f } },
                                                { "taperStart", 0.2f }, { "taperEnd", 0.9f }, { "opacityJitter", 0.5f } });
        paintOn(grass, grassBrush, 0);

        // Mandala: an arc petal wobbled, repeated 12 times around a centre, plus a star at its heart.
        // The petal: an arc centred off to the right of the flower's centre, bulging outwards.
        auto* petal = node("draw.arc", { { "x", 0.32f }, { "y", 0.3f }, { "radius", 0.07f }, { "start", -100.0f }, { "sweep", 200.0f } });
        auto* wobblyPetal = modify("draw.wobble", petal, { { "amount", 0.004f }, { "wavelength", 0.05f } });
        auto* flower = modify("draw.repeat.radial", wobblyPetal, { { "count", std::int64_t { 12 } }, { "centerX", 0.27f }, { "centerY", 0.3f } });
        paintOn(flower, node("draw.brush", { { "size", 0.006f }, { "hardness", 0.9f }, { "color", ns::Vec3Default { 1.0f, 0.75f, 0.3f } } }), 0);
        auto* heart = node("draw.star", { { "x", 0.27f }, { "y", 0.3f }, { "outerRadius", 0.06f }, { "innerRadius", 0.025f }, { "points", std::int64_t { 8 } } });
        paintOn(heart, node("draw.brush", { { "size", 0.004f }, { "color", ns::Vec3Default { 1.0f, 0.4f, 0.3f } } }), 2);

        // A chalk spiral, mirrored left to right.
        auto* coil = node("draw.spiral", { { "x", 0.62f }, { "y", 0.3f }, { "outerRadius", 0.15f }, { "turns", 3.0f } });
        auto* pair = modify("draw.mirror", coil, { { "position", 0.8f } });
        paintOn(pair, node("draw.brush", { { "tip", std::int64_t { 2 } }, { "size", 0.02f }, { "color", ns::Vec3Default { 0.9f, 0.9f, 1.0f } } }), 0);

        // A grid of small jittered squares turning a little more each row (a linear repeat of a grid row).
        auto* square = node("draw.rectangle", { { "x", 0.05f }, { "y", 0.52f }, { "width", 0.04f }, { "height", 0.04f } });
        auto* rowOf = modify("draw.repeat.grid", square, { { "columns", std::int64_t { 12 } }, { "rows", std::int64_t { 1 } }, { "dx", 0.075f } });
        auto* rows = modify("draw.repeat.linear", rowOf, { { "count", std::int64_t { 3 } }, { "dy", 0.07f }, { "rotateStep", 4.0f } });
        auto* shaky = modify("draw.jitter", rows, { { "amount", 0.004f } });
        paintOn(shaky, node("draw.brush", { { "size", 0.004f }, { "color", ns::Vec3Default { 0.5f, 0.7f, 1.0f } } }), 2);
    }
    else
    {
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
