// Image Graph smoke test: builds small graphs in code and checks the pixels against hand-worked values.
// Failures print and return 1 (crash dialogs are off).

#include "NoCrashDialogs.h"

#include <ImageGraph.h>

#include <cmath>
#include <iostream>

namespace ns = ce::node_system;

namespace
{
int failures = 0;

void expectPixel(const std::string& what, const image_graph::ImagePtr& image, int x, int y, std::vector<float> want, float tolerance = 1.0e-4f)
{
    if (image == nullptr)
    {
        std::cerr << "FAIL " << what << ": no image\n";
        ++failures;
        return;
    }
    const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(image->width) + static_cast<size_t>(x)) * 4;
    for (size_t i = 0; i < want.size(); ++i)
        if (std::abs(image->rgba[o + i] - want[i]) > tolerance)
        {
            std::cerr << "FAIL " << what << ": channel " << i << " got " << image->rgba[o + i] << " want " << want[i] << "\n";
            ++failures;
            return;
        }
    std::cout << "ok   " << what << "\n";
}

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
    {
        std::cerr << "FAIL could not connect " << out << " -> " << in << "\n";
        ++failures;
    }
}
}

int main()
{
    disableCrashDialogs();
    image_graph::Library library;
    ns::NodeTypeRegistry registry;
    library.registerTypes(registry);
    image_graph::Evaluator evaluator(library, {});
    if (! evaluator.isReady())
    {
        std::cerr << "FAIL Image Graph FRust did not load: " << evaluator.getError() << "\n";
        return 1;
    }
    std::cout << "ok   Image Graph FRust compiled from embedded source\n";

    ns::Graph graph("test");
    auto add = [&](const char* type) { return ns::AddRegisteredNode(graph, registry, type); };
    juce::String error;

    // Create Image 4 x 4 of (0.2, 0.4, 0.6); Invert -> (0.8, 0.6, 0.4).
    auto* create = add("image.create");
    set(create, "width", std::int64_t { 4 });
    set(create, "height", std::int64_t { 4 });
    set(create, "color", ns::Vec3Default { 0.2f, 0.4f, 0.6f });
    expectPixel("Create Image fills with its colour", evaluator.evaluate(graph, create->Id(), "image", error), 1, 2, { 0.2f, 0.4f, 0.6f, 1.0f });
    auto* invert = add("image.invert");
    connect(graph, create, "image", invert, "image");
    expectPixel("Invert", evaluator.evaluate(graph, invert->Id(), "image", error), 3, 3, { 0.8f, 0.6f, 0.4f, 1.0f });

    // Levels: grey 0.4 with input range 0.2..0.6 -> 0.5; with gamma 2 -> sqrt(0.5) = 0.707107.
    auto* grey = add("image.create");
    set(grey, "width", std::int64_t { 4 });
    set(grey, "height", std::int64_t { 4 });
    set(grey, "color", ns::Vec3Default { 0.4f, 0.4f, 0.4f });
    auto* levels = add("image.levels");
    connect(graph, grey, "image", levels, "image");
    set(levels, "inBlack", 0.2f);
    set(levels, "inWhite", 0.6f);
    expectPixel("Levels input range", evaluator.evaluate(graph, levels->Id(), "image", error), 0, 0, { 0.5f, 0.5f, 0.5f });
    set(levels, "gamma", 2.0f);
    expectPixel("Levels gamma (a changed setting recomputes)", evaluator.evaluate(graph, levels->Id(), "image", error), 0, 0,
                { 0.707107f, 0.707107f, 0.707107f });

    // Blend Multiply: 0.5 x 0.4 = 0.2 (both opaque).
    auto* half = add("image.create");
    set(half, "width", std::int64_t { 4 });
    set(half, "height", std::int64_t { 4 });
    set(half, "color", ns::Vec3Default { 0.5f, 0.5f, 0.5f });
    auto* blend = add("image.blend");
    connect(graph, half, "image", blend, "background");
    connect(graph, grey, "image", blend, "foreground");
    set(blend, "mode", std::int64_t { 1 });
    expectPixel("Blend Multiply", evaluator.evaluate(graph, blend->Id(), "image", error), 2, 1, { 0.2f, 0.2f, 0.2f, 1.0f });

    // Checker 4 x 4, 2 squares across, a = 0, b = 1: (0,0) = 0, (2,0) = 1, (2,2) = 0.
    auto* checker = add("image.checker");
    set(checker, "width", std::int64_t { 4 });
    set(checker, "height", std::int64_t { 4 });
    set(checker, "scale", std::int64_t { 2 });
    auto checkerImage = evaluator.evaluate(graph, checker->Id(), "image", error);
    expectPixel("Checker (0,0)", checkerImage, 0, 0, { 0.0f });
    expectPixel("Checker (2,0)", checkerImage, 2, 0, { 1.0f });
    expectPixel("Checker (2,2)", checkerImage, 2, 2, { 0.0f });

    // Gradient 5 x 1 left to right: 0, 0.5, 1 at x = 0, 2, 4. Sized from a wired image.
    auto* strip = add("image.create");
    set(strip, "width", std::int64_t { 5 });
    set(strip, "height", std::int64_t { 1 });
    auto* gradient = add("image.gradient");
    connect(graph, strip, "image", gradient, "image");
    auto gradientImage = evaluator.evaluate(graph, gradient->Id(), "image", error);
    expectPixel("Gradient start", gradientImage, 0, 0, { 0.0f });
    expectPixel("Gradient middle (size from the wired image)", gradientImage, 2, 0, { 0.5f });
    expectPixel("Gradient end", gradientImage, 4, 0, { 1.0f });

    // Noise: in 0..1, repeatable, cached, and changed by the seed.
    {
        auto* noise = add("image.noise");
        set(noise, "width", std::int64_t { 16 });
        set(noise, "height", std::int64_t { 16 });
        set(noise, "scale", std::int64_t { 4 });
        const auto first = evaluator.evaluate(graph, noise->Id(), "image", error);
        const auto again = evaluator.evaluate(graph, noise->Id(), "image", error);
        bool inRange = first != nullptr;
        for (size_t i = 0; inRange && i < first->rgba.size(); ++i)
            inRange = first->rgba[i] >= 0.0f && first->rgba[i] <= 1.0f;
        if (! inRange) { std::cerr << "FAIL noise out of 0..1\n"; ++failures; } else std::cout << "ok   noise stays in 0..1\n";
        if (first != again) { std::cerr << "FAIL noise not reused from the cache\n"; ++failures; } else std::cout << "ok   unchanged node is reused from the cache\n";
        set(noise, "seed", std::int64_t { 2 });
        const auto reseeded = evaluator.evaluate(graph, noise->Id(), "image", error);
        if (reseeded == nullptr || reseeded->rgba == first->rgba) { std::cerr << "FAIL a new seed did not change the noise\n"; ++failures; }
        else std::cout << "ok   a new seed changes the noise\n";
    }

    // Surface Map on a flat image: the normal points straight up (0.5, 0.5, 1).
    {
        auto* flat = add("image.create");
        set(flat, "width", std::int64_t { 16 });
        set(flat, "height", std::int64_t { 16 });
        auto* surface = add("image.surfacemap");
        connect(graph, flat, "image", surface, "image");
        expectPixel("Surface Map normal of a flat image", evaluator.evaluate(graph, surface->Id(), "normal", error), 5, 5, { 0.5f, 0.5f, 1.0f });
        expectPixel("Surface Map occlusion of a flat image", evaluator.evaluate(graph, surface->Id(), "occlusion", error), 5, 5, { 1.0f });
    }

    // Effects (image_fx.frust), each against hand-worked values. A helper builds Create Image -> effect.
    {
        auto source = [&](int w, int h, float r, float g, float b) {
            auto* n = add("image.create");
            set(n, "width", std::int64_t { w });
            set(n, "height", std::int64_t { h });
            set(n, "color", ns::Vec3Default { r, g, b });
            return n;
        };
        auto effect = [&](const char* type, ns::Node* from) {
            auto* n = add(type);
            if (n == nullptr) { std::cerr << "FAIL no node type " << type << "\n"; ++failures; return n; }
            connect(graph, from, "image", n, "image");
            return n;
        };
        auto run = [&](ns::Node* n) { return n != nullptr ? evaluator.evaluate(graph, n->Id(), "image", error) : nullptr; };

        auto* p = effect("image.posterize", source(2, 2, 0.3f, 0.6f, 0.3f));
        set(p, "levels", std::int64_t { 2 });
        expectPixel("Posterize 2 levels: 0.3 -> 0, 0.6 -> 1", run(p), 0, 0, { 0.0f, 1.0f, 0.0f });

        auto* t = effect("image.threshold", source(2, 2, 0.4f, 0.4f, 0.4f));
        expectPixel("Threshold 0.5: grey 0.4 -> black", run(t), 1, 1, { 0.0f, 0.0f, 0.0f });

        auto* bc = effect("image.brightnesscontrast", source(2, 2, 0.6f, 0.6f, 0.6f));
        set(bc, "brightness", 0.1f);
        set(bc, "contrast", 1.0f);
        expectPixel("Brightness +0.1, contrast 1: 0.6 -> 0.8", run(bc), 0, 0, { 0.8f, 0.8f, 0.8f });

        auto* sp = effect("image.sepia", source(2, 2, 0.5f, 0.5f, 0.5f));
        expectPixel("Sepia of grey 0.5", run(sp), 0, 0, { 0.6755f, 0.6015f, 0.4685f });

        auto* gm = effect("image.gradientmap", source(2, 2, 0.5f, 0.5f, 0.5f));
        set(gm, "dark", ns::Vec3Default { 0.0f, 0.0f, 0.0f });
        set(gm, "light", ns::Vec3Default { 1.0f, 0.0f, 0.0f });
        expectPixel("Gradient Map black -> red at 0.5", run(gm), 0, 0, { 0.5f, 0.0f, 0.0f });

        auto* hs = effect("image.huesaturation", source(2, 2, 1.0f, 0.0f, 0.0f));
        set(hs, "hue", 120.0f);
        expectPixel("Hue +120 degrees: red -> green", run(hs), 0, 0, { 0.0f, 1.0f, 0.0f }, 1.0e-3f);

        // Pixelate: a 2 x 2 image with red 0 on the left, 1 on the right -> one 2 x 2 block of 0.5.
        {
            auto* left = source(2, 2, 0.0f, 0.0f, 0.0f);
            auto* half = add("image.gradient");
            connect(graph, left, "image", half, "image");  // 2 x 2 left-to-right gradient: 0 then 1
            auto* px = effect("image.pixelate", half);
            set(px, "size", std::int64_t { 2 });
            expectPixel("Pixelate averages the block", run(px), 1, 0, { 0.5f, 0.5f, 0.5f });
        }

        expectPixel("Emboss of a flat image is mid grey", run(effect("image.emboss", source(4, 4, 0.7f, 0.2f, 0.4f))), 2, 2, { 0.5f });
        expectPixel("Edge Detect of a flat image is black", run(effect("image.edges", source(4, 4, 0.7f, 0.2f, 0.4f))), 2, 2, { 0.0f });
        expectPixel("Oil Paint of a flat image is unchanged", run(effect("image.oilpaint", source(8, 8, 0.7f, 0.2f, 0.4f))), 3, 3,
                    { 0.7f, 0.2f, 0.4f, 1.0f });

        // Dither to 2 levels on grey 0.5: Bayer threshold at (0,0) is -0.46875 -> 0, at (1,0) +0.03125 -> 1.
        auto dither = run(effect("image.dither", source(4, 4, 0.5f, 0.5f, 0.5f)));
        expectPixel("Dither (0,0)", dither, 0, 0, { 0.0f });
        expectPixel("Dither (1,0)", dither, 1, 0, { 1.0f });

        // Offset by a third on a 3 x 1 gradient 0, 0.5, 1 -> 1, 0, 0.5.
        {
            auto* strip3 = source(3, 1, 0.0f, 0.0f, 0.0f);
            auto* ramp = add("image.gradient");
            connect(graph, strip3, "image", ramp, "image");
            auto* off = effect("image.offset", ramp);
            set(off, "x", 1.0f / 3.0f);
            set(off, "y", 0.0f);
            auto shifted = run(off);
            expectPixel("Offset wraps: pixel 0 <- pixel 2", shifted, 0, 0, { 1.0f });
            expectPixel("Offset wraps: pixel 1 <- pixel 0", shifted, 1, 0, { 0.0f });
        }

        auto* sw = effect("image.swirl", source(4, 4, 0.3f, 0.6f, 0.9f));
        set(sw, "angle", 0.0f);
        expectPixel("Swirl of 0 degrees changes nothing", run(sw), 1, 2, { 0.3f, 0.6f, 0.9f, 1.0f });

        expectPixel("Vignette leaves the centre alone", run(effect("image.vignette", source(3, 3, 0.8f, 0.8f, 0.8f))), 1, 1, { 0.8f, 0.8f, 0.8f });
    }

    // Graph symbols and values (shared/NodeSystem/SYMBOLS.md): wired values replace typed-in settings.
    {
        ns::Symbol gamma;
        gamma.id = "gamma";
        gamma.name = "Gamma";
        gamma.kind = ns::SymbolKind::Param;
        gamma.type = ns::DataType::Float;
        gamma.value = 2.0f;
        graph.AddSymbol(gamma);

        auto* grey4 = add("image.create");
        set(grey4, "width", std::int64_t { 4 });
        set(grey4, "height", std::int64_t { 4 });
        set(grey4, "color", ns::Vec3Default { 0.4f, 0.4f, 0.4f });
        auto* lv = add("image.levels");
        connect(graph, grey4, "image", lv, "image");
        set(lv, "inBlack", 0.2f);
        set(lv, "inWhite", 0.6f);
        auto* get = ns::AddSymbolGetNode(graph, registry, gamma);
        connect(graph, get, "value", lv, "gamma");
        // (0.4 - 0.2) / 0.4 = 0.5, gamma 2 -> 0.5^(1/2) = 0.707107.
        expectPixel("A param wired into Levels gamma", evaluator.evaluate(graph, lv->Id(), "image", error), 0, 0, { 0.707107f });
        evaluator.getHost().paramOverrides["gamma"] = 1.0f;
        expectPixel("The param overridden from outside (gamma 1)", evaluator.evaluate(graph, lv->Id(), "image", error), 0, 0, { 0.5f });
        evaluator.getHost().paramOverrides.clear();

        // An Integer value node wired into Posterize levels: 2 levels, grey 0.6 -> 1.
        auto* grey6 = add("image.create");
        set(grey6, "width", std::int64_t { 2 });
        set(grey6, "height", std::int64_t { 2 });
        set(grey6, "color", ns::Vec3Default { 0.6f, 0.6f, 0.6f });
        auto* post = add("image.posterize");
        connect(graph, grey6, "image", post, "image");
        auto* levelsValue = add("image.value.integer");
        set(levelsValue, "value", std::int64_t { 2 });
        connect(graph, levelsValue, "value", post, "levels");
        expectPixel("An Integer value wired into Posterize levels", evaluator.evaluate(graph, post->Id(), "image", error), 0, 0, { 1.0f });

        // A Get node whose symbol is gone reports it.
        auto* lost = add("core.symbol.get.float");
        set(lost, "symbol", std::string("no_such_symbol"));
        ns::PinDefaultValue value;
        juce::String lostError;
        if (evaluator.evaluateValue(graph, lost->Id(), "value", value, lostError) || lostError.isEmpty())
        {
            std::cerr << "FAIL a Get node with a missing symbol did not report it\n";
            ++failures;
        }
        else
            std::cout << "ok   a missing symbol is reported\n";

        // Enums (shared/NodeSystem/enums.h): Blend's mode is a Blend Mode choice; a Choice param drives it.
        const auto* blendDescriptor = registry.Find("image.blend");
        if (registry.FindEnum("BlendMode") == nullptr || blendDescriptor == nullptr || blendDescriptor->inputs[2].type.enumType != "BlendMode")
        {
            std::cerr << "FAIL Blend's mode is not a Blend Mode choice\n";
            ++failures;
        }
        else
            std::cout << "ok   Blend's mode is a Blend Mode choice\n";

        ns::Symbol choice;
        choice.id = "blend_choice";
        choice.name = "Blend Choice";
        choice.kind = ns::SymbolKind::Param;
        choice.type = ns::DataType::Int;
        choice.enumType = "BlendMode";
        choice.value = std::int64_t { 1 }; // Multiply
        graph.AddSymbol(choice);
        auto* back = add("image.create");
        auto* front = add("image.create");
        for (auto* node : { back, front })
        {
            set(node, "width", std::int64_t { 2 });
            set(node, "height", std::int64_t { 2 });
        }
        set(back, "color", ns::Vec3Default { 0.5f, 0.5f, 0.5f });
        set(front, "color", ns::Vec3Default { 0.4f, 0.4f, 0.4f });
        auto* blend = add("image.blend");
        connect(graph, back, "image", blend, "background");
        connect(graph, front, "image", blend, "foreground");
        auto* mode = ns::AddSymbolGetNode(graph, registry, choice);
        connect(graph, mode, "value", blend, "mode");
        // Multiply: 0.5 x 0.4 = 0.2.
        expectPixel("A Blend Mode choice param (Multiply)", evaluator.evaluate(graph, blend->Id(), "image", error), 0, 0, { 0.2f });
        // Screen: 1 - (1 - 0.5)(1 - 0.4) = 0.7.
        evaluator.getHost().paramOverrides["blend_choice"] = std::int64_t { 2 };
        expectPixel("The choice overridden from outside (Screen)", evaluator.evaluate(graph, blend->Id(), "image", error), 0, 0, { 0.7f });
        evaluator.getHost().paramOverrides.clear();

        // A Blend Mode cannot wire into Ripple's direction (an Axis).
        auto* ripple = add("image.ripple");
        const auto* rippleDirection = [&]() -> const ns::Pin* {
            for (const auto& p : ripple->Inputs())
                if (p.name == "direction")
                    return &p;
            return nullptr;
        }();
        if (rippleDirection == nullptr || graph.Connect(mode->Id(), mode->Outputs().front().id, ripple->Id(), rippleDirection->id))
        {
            std::cerr << "FAIL a Blend Mode choice wired into an Axis setting\n";
            ++failures;
        }
        else
            std::cout << "ok   a Blend Mode choice does not wire into an Axis setting\n";
    }

    // Drawing (requirements section 5): shapes -> Paint with a Brush -> an image. All on a 10 x 10 opaque black canvas,
    // so a drawing point x maps to pixel x * 10 and pixel (i, j) covers i..i+1, j..j+1.
    {
        auto blackCanvas = [&]() {
            auto* n = add("image.create");
            set(n, "width", std::int64_t { 10 });
            set(n, "height", std::int64_t { 10 });
            set(n, "color", ns::Vec3Default { 0.0f, 0.0f, 0.0f });
            return n;
        };
        auto paint = [&](ns::Node* canvas, ns::Node* shape, ns::Node* brush, std::int64_t mode) {
            auto* p = add("draw.paint");
            if (canvas != nullptr) connect(graph, canvas, "image", p, "canvas");
            connect(graph, shape, "drawing", p, "drawing");
            if (brush != nullptr) connect(graph, brush, "brush", p, "brush");
            set(p, "mode", mode);
            return p;
        };
        auto image = [&](ns::Node* n) { return evaluator.evaluate(graph, n->Id(), "image", error); };

        // Stroke: a horizontal line at y 0.45 -> pixel row 4.5. A white hard brush 2 px across (size 0.2 of 10 px),
        // stamps every 0.2 px. Coverage runs from full within 0.5 px of the line to none at 1.5 px:
        //   row 4 (centre 4.5, distance 0) -> 1;  row 3 (distance 1) -> 0.5;  row 2 (distance 2) -> 0.
        auto* line = add("draw.line");
        set(line, "x1", 0.05f); set(line, "y1", 0.45f); set(line, "x2", 0.95f); set(line, "y2", 0.45f);
        auto* hard = add("draw.brush");
        set(hard, "size", 0.2f); set(hard, "hardness", 1.0f); set(hard, "spacing", 0.1f);
        auto* stroked = paint(blackCanvas(), line, hard, 0);
        expectPixel("Stroke: on the line", image(stroked), 4, 4, { 1.0f, 1.0f, 1.0f, 1.0f }, 1.0e-3f);
        expectPixel("Stroke: 1 px away, half covered", image(stroked), 4, 3, { 0.5f, 0.5f, 0.5f, 1.0f }, 1.0e-3f);
        expectPixel("Stroke: 2 px away, untouched", image(stroked), 4, 2, { 0.0f, 0.0f, 0.0f, 1.0f }, 1.0e-3f);

        // Fill: a rectangle x 0.25..0.65, y 0.2..0.6 -> pixels x 2.5..6.5, y 2..6, filled red.
        //   pixel (4, 3) is inside -> 1; (2, 3) and (6, 3) are half inside -> 0.5; (4, 6) is below -> 0.
        auto* rect = add("draw.rectangle");
        set(rect, "x", 0.25f); set(rect, "y", 0.2f); set(rect, "width", 0.4f); set(rect, "height", 0.4f);
        auto* filled = paint(blackCanvas(), rect, nullptr, 1);
        set(filled, "fill", ns::Vec3Default { 1.0f, 0.0f, 0.0f });
        expectPixel("Fill: inside", image(filled), 4, 3, { 1.0f, 0.0f, 0.0f, 1.0f }, 1.0e-3f);
        expectPixel("Fill: left edge half covered", image(filled), 2, 3, { 0.5f, 0.0f, 0.0f }, 1.0e-3f);
        expectPixel("Fill: right edge half covered", image(filled), 6, 3, { 0.5f, 0.0f, 0.0f }, 1.0e-3f);
        expectPixel("Fill: below the shape", image(filled), 4, 6, { 0.0f, 0.0f, 0.0f }, 1.0e-3f);

        // No canvas: Paint makes a transparent width x height image; a full-canvas fill gives exactly the fill colour.
        auto* whole = add("draw.rectangle");
        set(whole, "x", 0.0f); set(whole, "y", 0.0f); set(whole, "width", 1.0f); set(whole, "height", 1.0f);
        auto* fresh = paint(nullptr, whole, nullptr, 1);
        set(fresh, "width", std::int64_t { 4 });
        set(fresh, "height", std::int64_t { 4 });
        set(fresh, "fill", ns::Vec3Default { 0.2f, 0.4f, 0.6f });
        const auto freshImage = image(fresh);
        expectPixel("Paint without a canvas: fill colour, opaque", freshImage, 3, 3, { 0.2f, 0.4f, 0.6f, 1.0f }, 1.0e-3f);
        if (freshImage == nullptr || freshImage->width != 4 || freshImage->height != 4)
        {
            std::cerr << "FAIL Paint without a canvas did not make a 4 x 4 image\n";
            ++failures;
        }

        // Brush tips: one stamp (a zero-length line) at pixel (4.5, 4.5), size 0.4 -> radius 2 px, hardness 1, so
        // coverage is 1 up to 1.5 px from the centre and falls to 0 at 2.5 px.
        auto dot = [&]() {
            auto* n = add("draw.line");
            set(n, "x1", 0.45f); set(n, "y1", 0.45f); set(n, "x2", 0.45f); set(n, "y2", 0.45f);
            return n;
        };
        auto tipBrush = [&](std::int64_t tip, float angle) {
            auto* b = add("draw.brush");
            set(b, "tip", tip); set(b, "size", 0.4f); set(b, "hardness", 1.0f); set(b, "angle", angle);
            return b;
        };
        // Round: pixel (6, 6) is 2.83 px away -> 0. Square: its edge distance is max(2, 2) = 2 -> 0.5; (7, 4) is 3 -> 0.
        expectPixel("Round tip: corner pixel outside", image(paint(blackCanvas(), dot(), tipBrush(0, 0.0f), 0)), 6, 6, { 0.0f }, 1.0e-3f);
        auto* square = paint(blackCanvas(), dot(), tipBrush(1, 0.0f), 0);
        expectPixel("Square tip: corner pixel half covered", image(square), 6, 6, { 0.5f }, 1.0e-3f);
        expectPixel("Square tip: 3 px out, nothing", image(square), 7, 4, { 0.0f }, 1.0e-3f);
        // Square turned 45 degrees: (7, 4) is (3, 0) from the centre -> (2.1213, -2.1213) in the tip -> 2.5 - 2.1213 = 0.3787;
        // (6, 6) is (2, 2) -> (2.8284, 0) -> 0.
        auto* diamond = paint(blackCanvas(), dot(), tipBrush(1, 45.0f), 0);
        expectPixel("Square tip at 45 degrees: (7, 4)", image(diamond), 7, 4, { 0.37868f }, 1.0e-3f);
        expectPixel("Square tip at 45 degrees: (6, 6) outside", image(diamond), 6, 6, { 0.0f }, 1.0e-3f);
        // Image tip: a white 4 x 4 image stretched over the stamp's 4 x 4 px square: (5, 5) inside -> 1, (7, 5) outside -> 0.
        {
            auto* white = add("image.create");
            set(white, "width", std::int64_t { 4 });
            set(white, "height", std::int64_t { 4 });
            set(white, "color", ns::Vec3Default { 1.0f, 1.0f, 1.0f });
            auto* stamp = tipBrush(4, 0.0f);
            connect(graph, white, "image", stamp, "tipImage");
            auto* stamped = paint(blackCanvas(), dot(), stamp, 0);
            expectPixel("Image tip: inside the stamp", image(stamped), 5, 5, { 1.0f }, 1.0e-3f);
            expectPixel("Image tip: outside the stamp", image(stamped), 7, 5, { 0.0f }, 1.0e-3f);
        }

        // Taper start over the whole line (0.5 -> 9.5 px at row 4.5, size 0.2 -> radius 1): the stroke grows from nothing.
        // (0, 3) is 1 px from a line that is still thinner than that there -> 0; (9, 3) sits 1 px from the end stamp,
        // which has its full radius -> 0.5; (9, 4) on the line -> 1.
        {
            auto* taperLine = add("draw.line");
            set(taperLine, "x1", 0.05f); set(taperLine, "y1", 0.45f); set(taperLine, "x2", 0.95f); set(taperLine, "y2", 0.45f);
            auto* taper = add("draw.brush");
            set(taper, "size", 0.2f); set(taper, "hardness", 1.0f); set(taper, "taperStart", 1.0f);
            auto* tapered = paint(blackCanvas(), taperLine, taper, 0);
            expectPixel("Taper: thin start", image(tapered), 0, 3, { 0.0f }, 1.0e-3f);
            expectPixel("Taper: full end", image(tapered), 9, 3, { 0.5f }, 1.0e-3f);
            expectPixel("Taper: on the line at the end", image(tapered), 9, 4, { 1.0f }, 1.0e-3f);
        }

        // Randomness is repeatable: scatter with seed 1 twice gives the same pixels, seed 2 different ones.
        // Chalk never lays down more than the round tip it breaks up, and breaks up the middle of the stroke somewhere.
        {
            auto scattered = [&](std::int64_t seed, std::int64_t tip, float scatter) {
                auto* l = add("draw.line");
                set(l, "x1", 0.05f); set(l, "y1", 0.45f); set(l, "x2", 0.95f); set(l, "y2", 0.45f);
                auto* b = add("draw.brush");
                set(b, "tip", tip); set(b, "size", 0.3f); set(b, "hardness", 1.0f); set(b, "scatter", scatter); set(b, "seed", seed);
                set(b, "spacing", 0.5f);
                return image(paint(blackCanvas(), l, b, 0));
            };
            const auto a = scattered(1, 0, 0.5f), again = scattered(1, 0, 0.5f), other = scattered(2, 0, 0.5f);
            const bool same = a != nullptr && again != nullptr && a->rgba == again->rgba;
            const bool differs = a != nullptr && other != nullptr && a->rgba != other->rgba;
            if (! same || ! differs)
            {
                std::cerr << "FAIL scatter is not repeatable by seed (same " << same << ", differs " << differs << ")\n";
                ++failures;
            }
            else
                std::cout << "ok   scatter repeats with its seed and changes with another\n";

            const auto round = scattered(1, 0, 0.0f), chalk = scattered(1, 2, 0.0f);
            bool never_more = round != nullptr && chalk != nullptr, broken = false;
            for (size_t i = 0; never_more && i < round->rgba.size(); i += 4)
                never_more = chalk->rgba[i] <= round->rgba[i] + 1.0e-5f;
            for (int x = 1; never_more && x < 9; ++x)
                broken = broken || chalk->rgba[(4 * 10 + static_cast<size_t>(x)) * 4] < 0.99f;
            if (! never_more || ! broken)
            {
                std::cerr << "FAIL chalk (never more than round " << never_more << ", broken up " << broken << ")\n";
                ++failures;
            }
            else
                std::cout << "ok   chalk breaks up the round tip and never adds to it\n";
        }

        // Polygon: 3 sides, rotation 0 -> a corner straight up from the centre: (0.5, 0.5 - 0.25).
        auto* triangle = add("draw.polygon");
        set(triangle, "sides", std::int64_t { 3 });
        const auto shape = evaluator.evaluateDrawing(graph, triangle->Id(), "drawing", error);
        if (shape == nullptr || shape->paths.size() != 1 || shape->paths[0].points.size() != 3 || ! shape->paths[0].closed
            || std::abs(shape->paths[0].points[0].x - 0.5f) > 1.0e-5f || std::abs(shape->paths[0].points[0].y - 0.25f) > 1.0e-5f)
        {
            std::cerr << "FAIL a triangle does not start at its top corner\n";
            ++failures;
        }
        else
            std::cout << "ok   a triangle starts at its top corner\n";

        // Shapes and modifiers (milestone 3). Angles are clockwise on screen, y down.
        {
            auto drawingOf = [&](ns::Node* n) { return evaluator.evaluateDrawing(graph, n->Id(), "drawing", error); };
            auto check = [&](const char* what, bool good) {
                if (good)
                    std::cout << "ok   " << what << "\n";
                else
                {
                    std::cerr << "FAIL " << what << "\n";
                    ++failures;
                }
            };
            auto at = [](const drawing::DrawingPtr& d, size_t path, size_t point, float x, float y) {
                if (d == nullptr || path >= d->paths.size() || d->paths[path].points.empty())
                    return false;
                const auto& pts = d->paths[path].points;
                const auto& p = point == SIZE_MAX ? pts.back() : pts[point];
                return std::abs(p.x - x) < 1.0e-4f && std::abs(p.y - y) < 1.0e-4f;
            };
            auto lineFrom = [&](float x1, float y1, float x2, float y2) {
                auto* n = add("draw.line");
                set(n, "x1", x1); set(n, "y1", y1); set(n, "x2", x2); set(n, "y2", y2);
                return n;
            };
            auto modify = [&](const char* type, ns::Node* input) {
                auto* n = add(type);
                connect(graph, input, "drawing", n, "drawing");
                return n;
            };

            // Transform: a horizontal line through the centre turned 90 degrees about it runs top to bottom: (0.5, 0.1) first.
            auto* turned = modify("draw.transform", lineFrom(0.1f, 0.5f, 0.9f, 0.5f));
            set(turned, "rotation", 90.0f);
            check("Transform turns 90 degrees clockwise", at(drawingOf(turned), 0, 0, 0.5f, 0.1f) && at(drawingOf(turned), 0, 1, 0.5f, 0.9f));

            // Repeat Around: a spoke (0.5, 0.5) -> (0.7, 0.5), 4 copies: copy 1 ends at (0.5, 0.7), copy 2 at (0.3, 0.5).
            auto* around = modify("draw.repeat.radial", lineFrom(0.5f, 0.5f, 0.7f, 0.5f));
            set(around, "count", std::int64_t { 4 });
            const auto spokes = drawingOf(around);
            check("Repeat Around: 4 spokes, a quarter turn apart",
                  spokes != nullptr && spokes->paths.size() == 4 && at(spokes, 1, 1, 0.5f, 0.7f) && at(spokes, 2, 1, 0.3f, 0.5f));

            // Repeat in a Line: 3 copies 0.1 apart -> the third starts at x 0.7.
            auto* row = modify("draw.repeat.linear", lineFrom(0.5f, 0.5f, 0.7f, 0.5f));
            set(row, "count", std::int64_t { 3 });
            const auto rowDrawing = drawingOf(row);
            check("Repeat in a Line: 3 copies, the third 0.2 along", rowDrawing != nullptr && rowDrawing->paths.size() == 3 && at(rowDrawing, 2, 0, 0.7f, 0.5f));

            // Repeat in a Grid: 2 columns x 3 rows, 0.25 apart -> 6 copies, the last moved (0.25, 0.5).
            auto* grid = modify("draw.repeat.grid", lineFrom(0.5f, 0.5f, 0.7f, 0.5f));
            set(grid, "columns", std::int64_t { 2 });
            set(grid, "rows", std::int64_t { 3 });
            const auto gridDrawing = drawingOf(grid);
            check("Repeat in a Grid: 6 copies, the last at (0.75, 1.0)", gridDrawing != nullptr && gridDrawing->paths.size() == 6 && at(gridDrawing, 5, 0, 0.75f, 1.0f));

            // Mirror left-right about x 0.5, keeping the original: the copy of (0.5..0.7) ends at x 0.3.
            auto* mirror = modify("draw.mirror", lineFrom(0.5f, 0.5f, 0.7f, 0.5f));
            const auto mirrorDrawing = drawingOf(mirror);
            check("Mirror keeps the original and adds the flipped copy",
                  mirrorDrawing != nullptr && mirrorDrawing->paths.size() == 2 && at(mirrorDrawing, 1, 1, 0.3f, 0.5f));

            // Merge: two lines -> one drawing of two paths.
            auto* merge = add("draw.merge");
            connect(graph, lineFrom(0.0f, 0.0f, 1.0f, 1.0f), "drawing", merge, "a");
            connect(graph, lineFrom(1.0f, 0.0f, 0.0f, 1.0f), "drawing", merge, "c");
            check("Merge puts two drawings together", drawingOf(merge) != nullptr && drawingOf(merge)->paths.size() == 2);

            // Scatter: 10 copies, every copy's centre inside the area; the same seed repeats, another changes it.
            auto scatter = [&](std::int64_t seed) {
                auto* n = modify("draw.scatter", lineFrom(0.45f, 0.5f, 0.55f, 0.5f));
                set(n, "count", std::int64_t { 10 });
                set(n, "x", 0.2f); set(n, "y", 0.2f); set(n, "width", 0.6f); set(n, "height", 0.6f);
                set(n, "seed", seed);
                return drawingOf(n);
            };
            const auto s1 = scatter(1), s1again = scatter(1), s2 = scatter(2);
            bool inside = s1 != nullptr && s1->paths.size() == 10;
            for (size_t i = 0; inside && i < s1->paths.size(); ++i)
            {
                drawing::Drawing one;
                one.paths.push_back(s1->paths[i]);
                const auto c = drawing::bounds(one).centre();
                inside = c.x >= 0.2f - 1.0e-4f && c.x <= 0.8f + 1.0e-4f && c.y >= 0.2f - 1.0e-4f && c.y <= 0.8f + 1.0e-4f;
            }
            auto samePoints = [](const drawing::DrawingPtr& a, const drawing::DrawingPtr& b) {
                if (a == nullptr || b == nullptr || a->paths.size() != b->paths.size())
                    return false;
                for (size_t i = 0; i < a->paths.size(); ++i)
                    for (size_t k = 0; k < a->paths[i].points.size(); ++k)
                        if (a->paths[i].points[k].x != b->paths[i].points[k].x || a->paths[i].points[k].y != b->paths[i].points[k].y)
                            return false;
                return true;
            };
            check("Scatter: 10 copies, centres inside the area", inside);
            check("Scatter repeats with its seed and changes with another", samePoints(s1, s1again) && ! samePoints(s1, s2));

            // Jitter 0.1: every point within 0.1 of where it was.
            auto* jitter = modify("draw.jitter", lineFrom(0.1f, 0.5f, 0.9f, 0.5f));
            set(jitter, "amount", 0.1f);
            const auto jittered = drawingOf(jitter);
            check("Jitter stays within its amount", jittered != nullptr && std::abs(jittered->paths[0].points[0].x - 0.1f) <= 0.1f
                                                        && std::abs(jittered->paths[0].points[1].y - 0.5f) <= 0.1f
                                                        && ! at(jittered, 0, 0, 0.1f, 0.5f));

            // Wobble a line 0.1 -> 0.9 along y 0.5, amount 0.02, wavelength 0.1: points every 0.0125 -> 64 + the end = 65,
            // only pushed sideways (y within 0.48..0.52), starting at x 0.1 and ending at x 0.9.
            auto* wobble = modify("draw.wobble", lineFrom(0.1f, 0.5f, 0.9f, 0.5f));
            set(wobble, "amount", 0.02f);
            set(wobble, "wavelength", 0.1f);
            const auto wobbled = drawingOf(wobble);
            bool sideways = wobbled != nullptr && wobbled->paths.size() == 1 && wobbled->paths[0].points.size() == 65;
            for (size_t i = 0; sideways && i < wobbled->paths[0].points.size(); ++i)
                sideways = std::abs(wobbled->paths[0].points[i].y - 0.5f) <= 0.02f + 1.0e-5f;
            check("Wobble: 65 points, pushed only sideways, within its amount",
                  sideways && std::abs(wobbled->paths[0].points.front().x - 0.1f) < 1.0e-5f && std::abs(wobbled->paths[0].points.back().x - 0.9f) < 1.0e-5f);

            // Star 5 points: 10 corners, the first straight up at (0.5, 0.5 - 0.3).
            const auto starDrawing = drawingOf(add("draw.star"));
            check("Star: 10 corners, a point straight up", starDrawing != nullptr && starDrawing->paths[0].points.size() == 10 && at(starDrawing, 0, 0, 0.5f, 0.2f));
            // Arc from 0 through 90 degrees, radius 0.25: (0.75, 0.5) round to (0.5, 0.75).
            auto* quarter = add("draw.arc");
            set(quarter, "sweep", 90.0f);
            check("Arc: a quarter from right to bottom", at(drawingOf(quarter), 0, 0, 0.75f, 0.5f) && at(drawingOf(quarter), 0, SIZE_MAX, 0.5f, 0.75f));
            // Spiral 1 turn from the centre out to 0.2: ends at (0.7, 0.5).
            auto* coil = add("draw.spiral");
            set(coil, "outerRadius", 0.2f);
            set(coil, "turns", 1.0f);
            check("Spiral: from the centre to (0.7, 0.5)", at(drawingOf(coil), 0, 0, 0.5f, 0.5f) && at(drawingOf(coil), 0, SIZE_MAX, 0.7f, 0.5f));
            // Curve: from (0.1, 0.7) to (0.9, 0.3) exactly.
            auto* curve = add("draw.bezier");
            check("Curve: starts and ends on its end points", at(drawingOf(curve), 0, 0, 0.1f, 0.7f) && at(drawingOf(curve), 0, SIZE_MAX, 0.9f, 0.3f));
        }

        // Paint with nothing wired to draw reports it.
        auto* empty = add("draw.paint");
        juce::String paintError;
        if (evaluator.evaluate(graph, empty->Id(), "image", paintError) != nullptr || ! paintError.contains("drawing"))
        {
            std::cerr << "FAIL Paint without a drawing did not say so\n";
            ++failures;
        }
        else
            std::cout << "ok   Paint without a drawing says so\n";
    }

    // FRust pod generators (frust_image_demo): no hand-worked pixel values exist for these, so check that each gives a
    // full, varied image inside 0..1, and that Hills (height) is marked as data.
    for (const char* type : { "image.gen.wood", "image.gen.marble", "image.gen.hills_raw" })
    {
        auto* generator = add(type);
        if (generator == nullptr) { std::cerr << "FAIL no node type " << type << "\n"; ++failures; continue; }
        set(generator, "width", std::int64_t { 64 });
        set(generator, "height", std::int64_t { 64 });
        const auto image = evaluator.evaluate(graph, generator->Id(), "image", error);
        bool good = image != nullptr && image->width == 64 && image->height == 64;
        float lo = 1.0f, hi = 0.0f;
        for (size_t i = 0; good && i < image->rgba.size(); i += 4)
        {
            good = image->rgba[i] >= 0.0f && image->rgba[i] <= 1.0f && image->rgba[i + 3] > 0.0f;
            lo = std::min(lo, image->rgba[i]);
            hi = std::max(hi, image->rgba[i]);
        }
        good = good && hi - lo > 0.05f && (image->data == (std::string(type) == "image.gen.hills_raw"));
        if (! good) { std::cerr << "FAIL " << type << ": " << error << "\n"; ++failures; }
        else std::cout << "ok   " << type << " makes a varied 64 x 64 image\n";
    }

    if (error.isNotEmpty() && failures == 0)
        std::cout << "(last message: " << error << ")\n";
    if (failures > 0)
    {
        std::cerr << failures << " Image Graph check(s) FAILED\n";
        return 1;
    }
    std::cout << "Image Graph smoke passed.\n";
    return 0;
}
