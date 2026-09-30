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
