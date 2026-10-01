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
