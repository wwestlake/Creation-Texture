#include "ImageGraph.h"
#include "ImageLabCore.h"
#include "ImageLabFrustSources.h"
#include "TextureSet.h"
#include "ImageDemoHost.h"

#include <creation/frust/PluginRuntime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

namespace ns = ce::node_system;

namespace
{
extern "C" float ig_host_floor(float v) { return std::floor(v); }
extern "C" float ig_host_pow(float b, float e) { return std::pow(b, e); }
extern "C" float ig_host_sqrt(float v) { return std::sqrt(v); }
extern "C" float ig_host_i64_to_f32(std::int64_t v) { return static_cast<float>(v); }
extern "C" std::int64_t ig_host_f32_to_i64(float v) { return static_cast<std::int64_t>(v); }
extern "C" float ig_host_hash(std::int64_t x, std::int64_t y, std::int64_t seed)
{
    auto h = static_cast<std::uint64_t>(x) * 0x9E3779B97F4A7C15ull ^ static_cast<std::uint64_t>(y) * 0xC2B2AE3D27D4EB4Full
           ^ static_cast<std::uint64_t>(seed) * 0x165667B19E3779F9ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}

constexpr const char* key = "image_graph";
constexpr const char* demoKey = "frust_image_demo";
constexpr int maxDepth = 256;
}

namespace image_graph
{
//==============================================================================
class Routines final
{
public:
    Routines() : runtime("creation-texture")
    {
        runtime.registerHostFunction("ig_host_floor", reinterpret_cast<void*>(&ig_host_floor));
        runtime.registerHostFunction("ig_host_pow", reinterpret_cast<void*>(&ig_host_pow));
        runtime.registerHostFunction("ig_host_sqrt", reinterpret_cast<void*>(&ig_host_sqrt));
        runtime.registerHostFunction("ig_host_i64_to_f32", reinterpret_cast<void*>(&ig_host_i64_to_f32));
        runtime.registerHostFunction("ig_host_f32_to_i64", reinterpret_cast<void*>(&ig_host_f32_to_i64));
        runtime.registerHostFunction("ig_host_hash", reinterpret_cast<void*>(&ig_host_hash));

        ::frust::CompileRequest request;
        request.sources.push_back({ "image_graph.frust", std::string(ImageLabFrust::image_graph_frust, ImageLabFrust::image_graph_frustSize) });
        std::string loadError;
        if (! runtime.loadSource(key, request, loadError))
        {
            error = "Image Graph FRust routines did not compile: " + juce::String(loadError);
            return;
        }
        auto get = [this](const char* name) { return runtime.getFunction(key, name); };
        fill = reinterpret_cast<decltype(fill)>(get("ig_fill"));
        noise = reinterpret_cast<decltype(noise)>(get("ig_noise"));
        cells = reinterpret_cast<decltype(cells)>(get("ig_cells"));
        checker = reinterpret_cast<decltype(checker)>(get("ig_checker"));
        gradient = reinterpret_cast<decltype(gradient)>(get("ig_gradient"));
        invert = reinterpret_cast<decltype(invert)>(get("ig_invert"));
        grayscale = reinterpret_cast<decltype(grayscale)>(get("ig_grayscale"));
        levels = reinterpret_cast<decltype(levels)>(get("ig_levels"));
        if (! (fill && noise && cells && checker && gradient && invert && grayscale && levels))
        {
            error = "Image Graph FRust routines are incomplete.";
            return;
        }

        // The frust_image_demo pod (bundled copy of the Frate registry's 0.1.0). The plugin host needs an embedded
        // manifest, so one is put in front of the pod's own source, which is left exactly as published.
        image_demo_host::registerAll(runtime);
        ::frust::CompileRequest demo;
        const std::string manifest = "manifest \"{\\\"name\\\":\\\"frust_image_demo\\\",\\\"version\\\":\\\"0.1.0\\\","
                                     "\\\"description\\\":\\\"Procedural image generators (Frate pod, bundled).\\\","
                                     "\\\"entryPoints\\\":[],\\\"requiredHostFunctions\\\":[],"
                                     "\\\"intendedApplications\\\":[\\\"creation-texture\\\"]}\";\n";
        demo.sources.push_back({ "frust_image_demo/lib.fr", manifest + std::string(ImageLabFrust::lib_fr, ImageLabFrust::lib_frSize) });
        if (! runtime.loadSource(demoKey, demo, loadError))
            error = "The frust_image_demo generators did not compile: " + juce::String(loadError);
    }

    using DemoGenerator = void (*)(float*, std::int64_t, std::int64_t);
    DemoGenerator demoGenerator(const std::string& name)
    {
        return reinterpret_cast<DemoGenerator>(runtime.getFunction(demoKey, ("generate_" + name).c_str()));
    }

    creation::frust::PluginRuntime runtime;
    std::int64_t (*fill)(float*, std::int64_t, float, float, float, float) = nullptr;
    std::int64_t (*noise)(float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t) = nullptr;
    std::int64_t (*cells)(float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t) = nullptr;
    std::int64_t (*checker)(float*, std::int64_t, std::int64_t, std::int64_t, float, float) = nullptr;
    std::int64_t (*gradient)(float*, std::int64_t, std::int64_t, std::int64_t) = nullptr;
    std::int64_t (*invert)(float*, std::int64_t) = nullptr;
    std::int64_t (*grayscale)(float*, std::int64_t) = nullptr;
    std::int64_t (*levels)(float*, std::int64_t, float, float, float, float, float) = nullptr;
    image_lab::Compositor compositor;
    juce::String error;
};

//==============================================================================
namespace
{
const ns::PinDefaultValue* defaultOf(const ns::Node& node, const std::string& pin)
{
    for (const auto& p : node.Inputs())
        if (p.name == pin)
            return &p.defaultValue;
    return nullptr;
}

std::string valueText(const ns::PinDefaultValue& v)
{
    if (const auto* f = std::get_if<float>(&v)) return std::to_string(*f);
    if (const auto* i = std::get_if<std::int64_t>(&v)) return std::to_string(*i);
    if (const auto* b = std::get_if<bool>(&v)) return *b ? "true" : "false";
    if (const auto* s = std::get_if<std::string>(&v)) return *s;
    if (const auto* c = std::get_if<ns::Vec3Default>(&v)) return std::to_string(c->x) + "," + std::to_string(c->y) + "," + std::to_string(c->z);
    return "-";
}

std::shared_ptr<Image> blank(int width, int height, bool data)
{
    auto image = std::make_shared<Image>();
    image->width = juce::jlimit(1, 16384, width);
    image->height = juce::jlimit(1, 16384, height);
    image->rgba.assign(static_cast<size_t>(image->width) * static_cast<size_t>(image->height) * 4, 0.0f);
    image->data = data;
    return image;
}

std::shared_ptr<Image> copyOf(const ImagePtr& source)
{
    return std::make_shared<Image>(*source);
}

// Size for a generator: from its image input when wired, else its width / height settings.
std::shared_ptr<Image> generatorTarget(Context& c)
{
    auto found = c.inputs.find("image");
    if (found != c.inputs.end() && found->second != nullptr)
        return blank(found->second->width, found->second->height, false);
    return blank(c.integer("width", 1024), c.integer("height", 1024), false);
}

ns::PinSignature imageIn(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Texture }, std::string("") }; }
ns::PinSignature imageOut(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Texture }, {} }; }
ns::PinSignature intIn(const std::string& name, std::int64_t v) { return { name, { ns::PinKind::Data, ns::DataType::Int }, v }; }
ns::PinSignature floatIn(const std::string& name, float v) { return { name, { ns::PinKind::Data, ns::DataType::Float }, v }; }
ns::PinSignature boolIn(const std::string& name, bool v) { return { name, { ns::PinKind::Data, ns::DataType::Bool }, v }; }
ns::PinSignature colourIn(const std::string& name, float r, float g, float b) { return { name, { ns::PinKind::Data, ns::DataType::Color }, ns::Vec3Default { r, g, b } }; }
ns::PinSignature textIn(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::String }, std::string("") }; }

Definition define(std::string type, std::string name, std::string category, std::string description,
                  std::vector<ns::PinSignature> inputs, std::vector<ns::PinSignature> outputs,
                  std::function<bool(Context&, std::map<std::string, ImagePtr>&, juce::String&)> evaluate)
{
    Definition d;
    d.descriptor.typeName = std::move(type);
    d.descriptor.domain = ns::Domain::Core;
    d.descriptor.inputs = std::move(inputs);
    d.descriptor.outputs = std::move(outputs);
    d.descriptor.displayName = std::move(name);
    d.descriptor.category = std::move(category);
    d.descriptor.description = std::move(description);
    d.evaluate = std::move(evaluate);
    return d;
}

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

std::int64_t pixelCount(const Image& image) { return static_cast<std::int64_t>(image.width) * image.height; }

bool ok(std::int64_t result, const char* what, juce::String& error)
{
    if (result == 1)
        return true;
    error = juce::String(what) + " rejected its settings.";
    return false;
}

// One channel of an rgba image as a gray buffer, and back.
std::vector<float> channel(const Image& image, int c)
{
    std::vector<float> gray(static_cast<size_t>(pixelCount(image)));
    for (size_t i = 0; i < gray.size(); ++i)
        gray[i] = image.rgba[i * 4 + static_cast<size_t>(c)];
    return gray;
}

void setChannel(Image& image, int c, const std::vector<float>& gray)
{
    for (size_t i = 0; i < gray.size(); ++i)
        image.rgba[i * 4 + static_cast<size_t>(c)] = gray[i];
}

std::shared_ptr<Image> grayImage(const std::vector<float>& gray, int w, int h)
{
    auto image = blank(w, h, true);
    for (size_t i = 0; i < gray.size(); ++i)
    {
        image->rgba[i * 4] = image->rgba[i * 4 + 1] = image->rgba[i * 4 + 2] = gray[i];
        image->rgba[i * 4 + 3] = 1.0f;
    }
    return image;
}

std::shared_ptr<Image> rgbaImage(const std::vector<float>& rgba, int w, int h, bool data)
{
    auto image = blank(w, h, data);
    image->rgba = rgba;
    return image;
}

const char* surfaceSettingNames[] = { "sunken", "intensity", "sharpen", "noiseRemoval", "fine", "medium", "large",
                                      "veryLarge", "huge", "directX", "occlusion", "specularLevel",
                                      "specularContrast", "delight", "metallic" };
}

//==============================================================================
float Context::number(const std::string& pin, float fallback) const
{
    const auto* v = defaultOf(node, pin);
    if (v == nullptr) return fallback;
    if (const auto* f = std::get_if<float>(v)) return *f;
    if (const auto* i = std::get_if<std::int64_t>(v)) return static_cast<float>(*i);
    return fallback;
}

int Context::integer(const std::string& pin, int fallback) const
{
    const auto* v = defaultOf(node, pin);
    if (v == nullptr) return fallback;
    if (const auto* i = std::get_if<std::int64_t>(v)) return static_cast<int>(*i);
    if (const auto* f = std::get_if<float>(v)) return juce::roundToInt(*f);
    return fallback;
}

bool Context::flag(const std::string& pin, bool fallback) const
{
    const auto* v = defaultOf(node, pin);
    if (const auto* b = v != nullptr ? std::get_if<bool>(v) : nullptr) return *b;
    return fallback;
}

ns::Vec3Default Context::colour(const std::string& pin, ns::Vec3Default fallback) const
{
    const auto* v = defaultOf(node, pin);
    if (const auto* c = v != nullptr ? std::get_if<ns::Vec3Default>(v) : nullptr)
        return *c;
    return fallback;
}

juce::String Context::text(const std::string& pin) const
{
    const auto* v = defaultOf(node, pin);
    if (const auto* s = v != nullptr ? std::get_if<std::string>(v) : nullptr) return juce::String(*s);
    return {};
}

bool Context::wants(const std::string& output) const
{
    return std::find(wantedOutputs.begin(), wantedOutputs.end(), output) != wantedOutputs.end();
}

//==============================================================================
Library::Library()
{
    // --- Input ---
    definitions.push_back(define("image.create", "Create Image", "Input", "A blank image of the given size and colour.",
        { intIn("width", 1024), intIn("height", 1024), colourIn("color", 0.5f, 0.5f, 0.5f), floatIn("alpha", 1.0f) },
        { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto image = blank(c.integer("width", 1024), c.integer("height", 1024), false);
            const auto col = c.colour("color", { 0.5f, 0.5f, 0.5f });
            if (! ok(c.routines.fill(image->rgba.data(), pixelCount(*image), col.x, col.y, col.z,
                                      juce::jlimit(0.0f, 1.0f, c.number("alpha", 1.0f))), "Create Image", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.load", "Load Image", "Input", "An image from the project.",
        { imageIn("image") }, { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            const auto path = c.text("image");
            if (path.isEmpty()) { error = "Choose a project image in Properties."; return false; }
            auto image = c.host.loadImage ? c.host.loadImage(path) : nullptr;
            if (image == nullptr) { error = "Could not read " + path + "."; return false; }
            out["image"] = image;
            return true;
        }));

    // --- Generate ---
    auto sizeInputs = [](std::vector<ns::PinSignature> extra) {
        std::vector<ns::PinSignature> pins { imageIn("image"), intIn("width", 1024), intIn("height", 1024) };
        pins.insert(pins.end(), extra.begin(), extra.end());
        return pins;
    };

    definitions.push_back(define("image.noise", "Noise", "Generate",
        "Tiling fractal value noise. Size comes from the image wired in, or width / height.",
        sizeInputs({ intIn("scale", 8), intIn("octaves", 5), intIn("seed", 1) }), { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto image = generatorTarget(c);
            if (! ok(c.routines.noise(image->rgba.data(), image->width, image->height, juce::jmax(1, c.integer("scale", 8)),
                                       juce::jlimit(1, 12, c.integer("octaves", 5)), c.integer("seed", 1)), "Noise", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.cells", "Cells", "Generate", "Tiling cellular (Worley) pattern.",
        sizeInputs({ intIn("scale", 8), intIn("seed", 1) }), { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto image = generatorTarget(c);
            if (! ok(c.routines.cells(image->rgba.data(), image->width, image->height, juce::jmax(1, c.integer("scale", 8)),
                                       c.integer("seed", 1)), "Cells", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.checker", "Checker", "Generate", "Checkerboard of two greys.",
        sizeInputs({ intIn("scale", 8), floatIn("a", 0.0f), floatIn("b", 1.0f) }), { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto image = generatorTarget(c);
            if (! ok(c.routines.checker(image->rgba.data(), image->width, image->height, juce::jmax(1, c.integer("scale", 8)),
                                         c.number("a", 0.0f), c.number("b", 1.0f)), "Checker", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.gradient", "Gradient", "Generate", "0 to 1. Direction: 0 left-right, 1 top-bottom, 2 radial.",
        sizeInputs({ intIn("direction", 0) }), { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto image = generatorTarget(c);
            if (! ok(c.routines.gradient(image->rgba.data(), image->width, image->height, juce::jlimit(0, 2, c.integer("direction", 0))),
                     "Gradient", error))
                return false;
            out["image"] = image;
            return true;
        }));


    // --- Generate (FRust pod: frust_image_demo) --- one node per generator. Their output is display colour (sRGB),
    // converted to linear light here; Hills (height) is data.
    struct DemoInfo { const char* name; const char* display; bool data; };
    const DemoInfo demos[] = {
        { "hills", "Hills", false },
        { "hills_raw", "Hills (height)", true },
        { "clouds", "Clouds", false },
        { "planet", "Planet", false },
        { "caves", "Caves", false },
        { "ocean_floor", "Ocean Floor", false },
        { "wood", "Wood", false },
        { "marble", "Marble", false },
        { "cracks", "Cracks", false },
        { "stained_glass", "Stained Glass", false },
        { "rust", "Rust", false },
        { "plasma", "Plasma", false },
        { "tunnel", "Tunnel", false },
        { "mandelbrot", "Mandelbrot", false },
        { "julia", "Julia", false },
        { "stars", "Stars", false },
        { "heatmap", "Heatmap", false },
        { "truchet", "Truchet", false },
        { "tile", "Tile", false },
        { "album", "Album", false },
        { "reaction_diffusion", "Reaction-Diffusion", false },
        { "flow_field", "Flow Field", false },
        { "attractor", "Attractor", false }
    };
    for (const auto& demo : demos)
    {
        const std::string name = demo.name;
        const bool data = demo.data;
        const bool simulation = name == "reaction_diffusion" || name == "flow_field" || name == "attractor";
        definitions.push_back(define("image.gen." + name, demo.display, "Generate (FRust)",
            std::string("From the frust_image_demo pod. Size comes from the image wired in, or width / height.")
                + (simulation ? " A simulation: it is slow at large sizes." : ""),
            sizeInputs({}), { imageOut("image") },
            [name, data](Context& c, auto& out, juce::String& error) {
                auto generate = c.routines.demoGenerator(name);
                if (generate == nullptr) { error = "The generator is missing from the pod."; return false; }
                auto image = generatorTarget(c);
                image->data = data;
                generate(image->rgba.data(), image->width, image->height);
                image_demo_host::freeArena();
                if (! data)
                    for (size_t i = 0; i < image->rgba.size(); i += 4)
                        for (size_t k = 0; k < 3; ++k)
                            image->rgba[i + k] = srgbToLinear(juce::jlimit(0.0f, 1.0f, image->rgba[i + k]));
                out["image"] = image;
                return true;
            }));
    }

    // --- Adjust ---
    auto needInput = [](Context& c, const char* pin, juce::String& error) -> ImagePtr {
        auto found = c.inputs.find(pin);
        if (found == c.inputs.end() || found->second == nullptr)
        {
            error = juce::String("Connect an image to ") + pin + ".";
            return nullptr;
        }
        return found->second;
    };

    definitions.push_back(define("image.invert", "Invert", "Adjust", "1 - colour; alpha kept.",
        { imageIn("image") }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto image = copyOf(input);
            if (! ok(c.routines.invert(image->rgba.data(), pixelCount(*image)), "Invert", error)) return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.grayscale", "Grayscale", "Adjust", "Luminance into all channels.",
        { imageIn("image") }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto image = copyOf(input);
            if (! ok(c.routines.grayscale(image->rgba.data(), pixelCount(*image)), "Grayscale", error)) return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.levels", "Levels", "Adjust", "Input range, gamma, output range - like GIMP's Levels.",
        { imageIn("image"), floatIn("inBlack", 0.0f), floatIn("inWhite", 1.0f), floatIn("gamma", 1.0f),
          floatIn("outBlack", 0.0f), floatIn("outWhite", 1.0f) },
        { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto image = copyOf(input);
            if (! ok(c.routines.levels(image->rgba.data(), pixelCount(*image), c.number("inBlack", 0.0f), c.number("inWhite", 1.0f),
                                        c.number("gamma", 1.0f), c.number("outBlack", 0.0f), c.number("outWhite", 1.0f)), "Levels", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(define("image.blur", "Blur", "Adjust", "Smooth (about Gaussian) blur, wrapping at the edges.",
        { imageIn("image"), floatIn("radius", 4.0f) }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto image = copyOf(input);
            const int radius = juce::jlimit(0, juce::jmax(0, (juce::jmin(image->width, image->height) - 1) / 2),
                                            juce::roundToInt(c.number("radius", 4.0f)));
            if (radius > 0)
                for (int ch = 0; ch < 4; ++ch)
                {
                    auto plane = channel(*image, ch);
                    std::vector<float> a, b;
                    if (! c.surfaceMaps.boxBlur(plane, a, image->width, image->height, radius, error)
                        || ! c.surfaceMaps.boxBlur(a, b, image->width, image->height, radius, error)
                        || ! c.surfaceMaps.boxBlur(b, plane, image->width, image->height, radius, error))
                        return false;
                    setChannel(*image, ch, plane);
                }
            out["image"] = image;
            return true;
        }));

    // --- Combine ---
    definitions.push_back(define("image.blend", "Blend", "Combine",
        "Foreground over background. Mode: 0 Normal, 1 Multiply, 2 Screen, 3 Overlay, 4 Add, 5 Subtract, 6 Darken, 7 Lighten, 8 Difference.",
        { imageIn("background"), imageIn("foreground"), intIn("mode", 0), floatIn("opacity", 1.0f) }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto back = needInput(c, "background", error);
            auto front = back != nullptr ? needInput(c, "foreground", error) : nullptr;
            if (front == nullptr) return false;
            if (front->width != back->width || front->height != back->height)
            {
                error = "Blend needs two images of the same size.";
                return false;
            }
            auto image = copyOf(back);
            if (! c.routines.compositor.composite(image->rgba.data(), front->rgba.data(), pixelCount(*image),
                                                   static_cast<image_lab::BlendMode>(juce::jlimit(0, 8, c.integer("mode", 0))),
                                                   juce::jlimit(0.0f, 1.0f, c.number("opacity", 1.0f)), error))
                return false;
            out["image"] = image;
            return true;
        }));

    // --- Surface ---
    {
        const surface_maps::Settings d;
        std::vector<ns::PinSignature> inputs { imageIn("image"), textIn("surfaceMap") };
        inputs.push_back(boolIn("sunken", d.sunken));
        inputs.push_back(floatIn("intensity", d.intensity));
        inputs.push_back(floatIn("sharpen", d.sharpen));
        inputs.push_back(floatIn("noiseRemoval", d.noiseRemoval));
        inputs.push_back(floatIn("fine", d.fine));
        inputs.push_back(floatIn("medium", d.medium));
        inputs.push_back(floatIn("large", d.large));
        inputs.push_back(floatIn("veryLarge", d.veryLarge));
        inputs.push_back(floatIn("huge", d.huge));
        inputs.push_back(boolIn("directX", d.directX));
        inputs.push_back(floatIn("occlusion", d.occlusion));
        inputs.push_back(floatIn("specularLevel", d.specularLevel));
        inputs.push_back(floatIn("specularContrast", d.specularContrast));
        inputs.push_back(floatIn("delight", d.delight));
        inputs.push_back(floatIn("metallic", d.metallic));

        definitions.push_back(define("image.surfacemap", "Surface Map", "Surface",
            "CrazyBump-style maps from the image wired in. Its settings can be loaded from a saved surface map "
            "(surfaceMap) and changed. Only the outputs that are connected are computed.",
            inputs,
            { imageOut("normal"), imageOut("height"), imageOut("occlusion"), imageOut("roughness"), imageOut("specular"),
              imageOut("diffuse"), imageOut("orm") },
            [needInput](Context& c, auto& out, juce::String& error) {
                auto input = needInput(c, "image", error);
                if (input == nullptr) return false;

                surface_maps::Settings s;
                s.sunken = c.flag("sunken", s.sunken);
                s.intensity = c.number("intensity", s.intensity);
                s.sharpen = c.number("sharpen", s.sharpen);
                s.noiseRemoval = c.number("noiseRemoval", s.noiseRemoval);
                s.fine = c.number("fine", s.fine);
                s.medium = c.number("medium", s.medium);
                s.large = c.number("large", s.large);
                s.veryLarge = c.number("veryLarge", s.veryLarge);
                s.huge = c.number("huge", s.huge);
                s.directX = c.flag("directX", s.directX);
                s.occlusion = c.number("occlusion", s.occlusion);
                s.specularLevel = c.number("specularLevel", s.specularLevel);
                s.specularContrast = c.number("specularContrast", s.specularContrast);
                s.delight = c.number("delight", s.delight);
                s.metallic = c.number("metallic", s.metallic);

                surface_maps::Wanted wanted;
                wanted.height = c.wants("height");
                wanted.normal = c.wants("normal");
                wanted.occlusion = c.wants("occlusion") || c.wants("orm");
                wanted.specular = c.wants("roughness") || c.wants("specular") || c.wants("orm");
                wanted.diffuse = c.wants("diffuse");

                surface_maps::Maps maps;
                if (! c.surfaceMaps.compute(input->rgba, input->width, input->height, s, maps, error, {}, wanted))
                    return false;

                const int w = maps.width, h = maps.height;
                if (wanted.normal) out["normal"] = rgbaImage(maps.normal, w, h, true);
                if (c.wants("height")) out["height"] = grayImage(maps.heightMap, w, h);
                if (c.wants("occlusion")) out["occlusion"] = grayImage(maps.occlusion, w, h);
                if (c.wants("roughness")) out["roughness"] = grayImage(maps.roughness, w, h);
                if (c.wants("specular")) out["specular"] = grayImage(maps.specular, w, h);
                if (wanted.diffuse) out["diffuse"] = rgbaImage(maps.diffuse, w, h, false);
                if (c.wants("orm"))
                {
                    auto orm = blank(w, h, true);
                    for (size_t i = 0; i < maps.occlusion.size(); ++i)
                    {
                        orm->rgba[i * 4] = maps.occlusion[i];
                        orm->rgba[i * 4 + 1] = maps.roughness[i];
                        orm->rgba[i * 4 + 2] = maps.metallic;
                        orm->rgba[i * 4 + 3] = 1.0f;
                    }
                    out["orm"] = orm;
                }
                return true;
            }));
    }
}

void Library::registerTypes(ns::NodeTypeRegistry& registry) const
{
    for (const auto& d : definitions)
        registry.Register(d.descriptor);
}

const Definition* Library::find(const std::string& typeName) const
{
    for (const auto& d : definitions)
        if (d.descriptor.typeName == typeName)
            return &d;
    return nullptr;
}

//==============================================================================
Evaluator::Evaluator(const Library& lib, Host h)
    : library(lib), host(std::move(h)), routines(std::make_unique<Routines>()), surfaceMaps(std::make_unique<surface_maps::Engine>())
{
}

Evaluator::~Evaluator() = default;

bool Evaluator::isReady() const noexcept
{
    return routines->error.isEmpty() && routines->compositor.isReady() && surfaceMaps->isReady();
}

juce::String Evaluator::getError() const
{
    if (routines->error.isNotEmpty()) return routines->error;
    if (! routines->compositor.isReady()) return routines->compositor.getError();
    return surfaceMaps->getError();
}

void Evaluator::clearCache()
{
    cache.clear();
}

ImagePtr Evaluator::evaluate(const ns::Graph& graph, ns::NodeId node, const std::string& output, juce::String& error)
{
    if (! isReady())
    {
        error = getError();
        return nullptr;
    }
    std::string signature;
    if (! evaluateNode(graph, node, { output }, signature, error, 0))
        return nullptr;
    auto found = cache[node].outputs.find(output);
    if (found == cache[node].outputs.end() || found->second == nullptr)
    {
        error = "The node has no output called " + juce::String(output) + ".";
        return nullptr;
    }
    return found->second;
}

bool Evaluator::evaluateNode(const ns::Graph& graph, ns::NodeId id, const std::vector<std::string>& wanted,
                             std::string& signatureOut, juce::String& error, int depth)
{
    if (depth > maxDepth)
    {
        error = "The graph loops back on itself.";
        return false;
    }
    const auto* node = graph.FindNode(id);
    if (node == nullptr)
    {
        error = "A node is missing from the graph.";
        return false;
    }
    const auto* definition = library.find(node->TypeName());
    if (definition == nullptr)
    {
        error = "Unknown node type " + juce::String(node->TypeName()) + ".";
        return false;
    }

    // Wired image inputs first (each computes only the output it is asked for), building this node's signature.
    Context context { *node, host, *routines, *surfaceMaps, {}, {} };
    std::string signature = node->TypeName();
    for (const auto& pin : node->Inputs())
    {
        const ns::Connection* wire = nullptr;
        for (const auto& connection : graph.Connections())
            if (connection.toNode == id && connection.toPin == pin.id)
                wire = &connection;

        if (wire == nullptr)
        {
            signature += "|" + pin.name + "=" + valueText(pin.defaultValue);
            continue;
        }

        const auto* from = graph.FindNode(wire->fromNode);
        const auto* fromPin = from != nullptr ? from->FindPin(wire->fromPin) : nullptr;
        if (fromPin == nullptr)
            continue;
        std::string upstream;
        if (! evaluateNode(graph, wire->fromNode, { fromPin->name }, upstream, error, depth + 1))
            return false;
        context.inputs[pin.name] = cache[wire->fromNode].outputs[fromPin->name];
        signature += "|" + pin.name + "<" + upstream + ":" + fromPin->name;
    }
    signatureOut = std::to_string(std::hash<std::string> {}(signature));

    // Reuse the cache when nothing that feeds this node has changed and it already has what is wanted.
    auto& cached = cache[id];
    std::set<std::string> needed(wanted.begin(), wanted.end());
    if (cached.signature == signatureOut)
    {
        bool complete = true;
        for (const auto& name : needed)
            complete = complete && cached.outputs.count(name) > 0;
        if (complete)
            return true;
        for (const auto& [name, image] : cached.outputs)
            needed.insert(name); // recompute together with what was already there
    }

    context.wantedOutputs.assign(needed.begin(), needed.end());
    std::map<std::string, ImagePtr> outputs;
    juce::String nodeError;
    if (! definition->evaluate(context, outputs, nodeError))
    {
        error = juce::String(definition->descriptor.displayName) + ": " + nodeError;
        cached = {};
        return false;
    }
    cached.signature = signatureOut;
    cached.outputs = std::move(outputs);
    return true;
}

//==============================================================================
juce::Image toDisplayImage(const Image& image)
{
    if (image.width <= 0 || image.height <= 0)
        return {};
    if (! image.data)
        return image_lab::imageFromPixels(image.rgba, image.width, image.height);

    juce::Image out(juce::Image::ARGB, image.width, image.height, true);
    juce::Image::BitmapData bits(out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x)
        {
            const size_t o = (static_cast<size_t>(y) * static_cast<size_t>(image.width) + static_cast<size_t>(x)) * 4;
            bits.setPixelColour(x, y, juce::Colour::fromFloatRGBA(juce::jlimit(0.0f, 1.0f, image.rgba[o]), juce::jlimit(0.0f, 1.0f, image.rgba[o + 1]),
                                                                 juce::jlimit(0.0f, 1.0f, image.rgba[o + 2]), juce::jlimit(0.0f, 1.0f, image.rgba[o + 3])));
        }
    return out;
}

ImagePtr fromDisplayImage(const juce::Image& source)
{
    const auto layer = image_lab::layerFromImage(source, {});
    if (! layer.isValid())
        return nullptr;
    auto image = std::make_shared<Image>();
    image->width = layer.width;
    image->height = layer.height;
    image->rgba = layer.pixels;
    return image;
}
}
