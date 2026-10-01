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

extern "C" float fx_host_floor(float v) { return std::floor(v); }
extern "C" float fx_host_sqrt(float v) { return std::sqrt(v < 0.0f ? 0.0f : v); }
extern "C" float fx_host_sin(float v) { return std::sin(v); }
extern "C" float fx_host_cos(float v) { return std::cos(v); }
extern "C" float fx_host_atan2(float y, float x) { return std::atan2(y, x); }
extern "C" float fx_host_i64_to_f32(std::int64_t v) { return static_cast<float>(v); }
extern "C" std::int64_t fx_host_f32_to_i64(float v) { return static_cast<std::int64_t>(v); }
extern "C" float fx_host_hash(std::int64_t x, std::int64_t y, std::int64_t seed) { return ig_host_hash(x, y, seed); }

constexpr const char* key = "image_graph";
constexpr const char* demoKey = "frust_image_demo";
constexpr const char* fxKey = "image_fx";
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


        // Image effects (image_fx.frust - to become the frust_image_fx pod).
        runtime.registerHostFunction("fx_host_floor", reinterpret_cast<void*>(&fx_host_floor));
        runtime.registerHostFunction("fx_host_sqrt", reinterpret_cast<void*>(&fx_host_sqrt));
        runtime.registerHostFunction("fx_host_sin", reinterpret_cast<void*>(&fx_host_sin));
        runtime.registerHostFunction("fx_host_cos", reinterpret_cast<void*>(&fx_host_cos));
        runtime.registerHostFunction("fx_host_atan2", reinterpret_cast<void*>(&fx_host_atan2));
        runtime.registerHostFunction("fx_host_i64_to_f32", reinterpret_cast<void*>(&fx_host_i64_to_f32));
        runtime.registerHostFunction("fx_host_f32_to_i64", reinterpret_cast<void*>(&fx_host_f32_to_i64));
        runtime.registerHostFunction("fx_host_hash", reinterpret_cast<void*>(&fx_host_hash));
        ::frust::CompileRequest fx;
        fx.sources.push_back({ "image_fx.frust", std::string(ImageLabFrust::image_fx_frust, ImageLabFrust::image_fx_frustSize) });
        if (! runtime.loadSource(fxKey, fx, loadError))
        {
            error = "Image effect FRust routines did not compile: " + juce::String(loadError);
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


    // An image-effect routine by name (image_fx.frust).
    template <typename Fn>
    Fn fx(const char* name)
    {
        return reinterpret_cast<Fn>(runtime.getFunction(fxKey, name));
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
// An integer setting whose values are named by one of the library's enums (a dropdown in Properties).
ns::PinSignature enumIn(const std::string& name, const std::string& enumName, std::int64_t v)
{
    ns::PinSignature pin { name, { ns::PinKind::Data, ns::DataType::Int }, v };
    pin.type.enumType = enumName;
    return pin;
}
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
const ns::PinDefaultValue* Context::setting(const std::string& pin) const
{
    auto wired = wiredValues.find(pin);
    if (wired != wiredValues.end())
        return &wired->second;
    return defaultOf(node, pin);
}

float Context::number(const std::string& pin, float fallback) const
{
    const auto* v = setting(pin);
    if (v == nullptr) return fallback;
    if (const auto* f = std::get_if<float>(v)) return *f;
    if (const auto* i = std::get_if<std::int64_t>(v)) return static_cast<float>(*i);
    return fallback;
}

int Context::integer(const std::string& pin, int fallback) const
{
    const auto* v = setting(pin);
    if (v == nullptr) return fallback;
    if (const auto* i = std::get_if<std::int64_t>(v)) return static_cast<int>(*i);
    if (const auto* f = std::get_if<float>(v)) return juce::roundToInt(*f);
    return fallback;
}

bool Context::flag(const std::string& pin, bool fallback) const
{
    const auto* v = setting(pin);
    if (const auto* b = v != nullptr ? std::get_if<bool>(v) : nullptr) return *b;
    return fallback;
}

ns::Vec3Default Context::colour(const std::string& pin, ns::Vec3Default fallback) const
{
    const auto* v = setting(pin);
    if (const auto* c = v != nullptr ? std::get_if<ns::Vec3Default>(v) : nullptr)
        return *c;
    return fallback;
}

juce::String Context::text(const std::string& pin) const
{
    const auto* v = setting(pin);
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

    // --- Enums --- the named choices integer settings use. Order is the value: variant i is i.
    enums.push_back({ "BlendMode", "Blend Mode",
                      { "Normal", "Multiply", "Screen", "Overlay", "Add", "Subtract", "Darken", "Lighten", "Difference" },
                      "How a foreground image is combined with the background." });
    enums.push_back({ "GradientDirection", "Gradient Direction", { "Left to Right", "Top to Bottom", "Radial" },
                      "Which way a gradient runs." });
    enums.push_back({ "Axis", "Axis", { "Horizontal", "Vertical" }, "A horizontal or vertical direction." });

    // --- Values --- one value, typed in, wired into any setting of the same type.
    auto valueNode = [](std::string type, std::string name, ns::PinSignature pin, std::string description) {
        ns::PinSignature output { "value", pin.type, {} };
        const auto pinName = pin.name;
        return define(std::move(type), std::move(name), "Values", std::move(description), { pin }, { output },
            [pinName](Context& c, auto&, juce::String&) {
                if (const auto* v = c.setting(pinName))
                    c.valueOutputs["value"] = *v;
                return true;
            });
    };
    definitions.push_back(valueNode("image.value.number", "Number", floatIn("value", 0.5f), "A number, wired into any number setting."));
    definitions.push_back(valueNode("image.value.integer", "Integer", intIn("value", 1), "A whole number, wired into any integer setting."));
    definitions.push_back(valueNode("image.value.toggle", "Toggle", boolIn("value", false), "On or off, wired into any toggle setting."));
    definitions.push_back(valueNode("image.value.color", "Color", colourIn("value", 1.0f, 1.0f, 1.0f), "A colour, wired into any colour setting."));

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

    definitions.push_back(define("image.gradient", "Gradient", "Generate", "0 to 1, left to right, top to bottom, or out from the centre.",
        sizeInputs({ enumIn("direction", "GradientDirection", 0) }), { imageOut("image") },
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


    // ---------------------------------------------------------------- effects (image_fx.frust)
    using InPlace = std::int64_t (*)(float*, std::int64_t);
    using Neighbour = std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t);

    // A node whose routine changes a copy of its input in place.
    auto inPlace = [needInput](std::string type, std::string name, std::string category, std::string description,
                               std::vector<ns::PinSignature> settings,
                               std::function<std::int64_t(Context&, Image&)> run) {
        std::vector<ns::PinSignature> pins { imageIn("image") };
        pins.insert(pins.end(), settings.begin(), settings.end());
        return define(std::move(type), std::move(name), std::move(category), std::move(description), pins, { imageOut("image") },
            [needInput, run, name](Context& c, auto& out, juce::String& error) {
                auto input = needInput(c, "image", error);
                if (input == nullptr) return false;
                auto image = copyOf(input);
                if (! ok(run(c, *image), name.c_str(), error)) return false;
                out["image"] = image;
                return true;
            });
    };

    // A node whose routine reads its input (with neighbours) into a new image of the same size.
    auto sourceToDest = [needInput](std::string type, std::string name, std::string category, std::string description,
                                    std::vector<ns::PinSignature> settings,
                                    std::function<std::int64_t(Context&, const Image&, Image&)> run) {
        std::vector<ns::PinSignature> pins { imageIn("image") };
        pins.insert(pins.end(), settings.begin(), settings.end());
        return define(std::move(type), std::move(name), std::move(category), std::move(description), pins, { imageOut("image") },
            [needInput, run, name](Context& c, auto& out, juce::String& error) {
                auto input = needInput(c, "image", error);
                if (input == nullptr) return false;
                auto image = blank(input->width, input->height, input->data);
                if (! ok(run(c, *input, *image), name.c_str(), error)) return false;
                out["image"] = image;
                return true;
            });
    };

    // --- Color ---
    definitions.push_back(inPlace("image.posterize", "Posterize", "Color", "Each channel to a few evenly spaced values.",
        { intIn("levels", 4) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, std::int64_t)>("fx_posterize")(img.rgba.data(), pixelCount(img), c.integer("levels", 4));
        }));
    definitions.push_back(inPlace("image.threshold", "Threshold", "Color", "Black below the level, white at or above it.",
        { floatIn("level", 0.5f) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float)>("fx_threshold")(img.rgba.data(), pixelCount(img), c.number("level", 0.5f));
        }));
    definitions.push_back(inPlace("image.brightnesscontrast", "Brightness / Contrast", "Color", "Brightness and contrast, each -1 to 1.",
        { floatIn("brightness", 0.0f), floatIn("contrast", 0.0f) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float, float)>("fx_brightness_contrast")(
                img.rgba.data(), pixelCount(img), c.number("brightness", 0.0f), c.number("contrast", 0.0f));
        }));
    definitions.push_back(inPlace("image.huesaturation", "Hue / Saturation", "Color", "Hue shift in degrees; saturation and lightness -1 to 1.",
        { floatIn("hue", 0.0f), floatIn("saturation", 0.0f), floatIn("lightness", 0.0f) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float, float, float)>("fx_hue_saturation")(
                img.rgba.data(), pixelCount(img), c.number("hue", 0.0f) / 360.0f, c.number("saturation", 0.0f), c.number("lightness", 0.0f));
        }));
    definitions.push_back(inPlace("image.colorize", "Colorize", "Color", "Brightness tinted with a colour.",
        { colourIn("color", 1.0f, 0.8f, 0.6f) }, [](Context& c, Image& img) {
            const auto col = c.colour("color", { 1.0f, 0.8f, 0.6f });
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float, float, float)>("fx_colorize")(img.rgba.data(), pixelCount(img), col.x, col.y, col.z);
        }));
    definitions.push_back(inPlace("image.sepia", "Sepia", "Color", "Old-photo brown tone; amount 0 to 1.",
        { floatIn("amount", 1.0f) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float)>("fx_sepia")(img.rgba.data(), pixelCount(img), c.number("amount", 1.0f));
        }));
    definitions.push_back(inPlace("image.gradientmap", "Gradient Map", "Color", "Brightness mapped from the dark colour to the light colour.",
        { colourIn("dark", 0.05f, 0.05f, 0.2f), colourIn("light", 1.0f, 0.9f, 0.6f) }, [](Context& c, Image& img) {
            const auto a = c.colour("dark", { 0.0f, 0.0f, 0.0f });
            const auto b = c.colour("light", { 1.0f, 1.0f, 1.0f });
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, float, float, float, float, float, float)>("fx_gradient_map")(
                img.rgba.data(), pixelCount(img), a.x, a.y, a.z, b.x, b.y, b.z);
        }));

    // --- Artistic ---
    definitions.push_back(sourceToDest("image.pixelate", "Pixelate", "Artistic", "Blocks of the given size, each its average colour.",
        { intIn("size", 8) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_pixelate")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, juce::jmax(1, c.integer("size", 8)));
        }));
    definitions.push_back(sourceToDest("image.emboss", "Emboss", "Artistic", "Raised-relief grey from brightness changes.",
        { floatIn("strength", 4.0f) }, [](Context& c, const Image& in, Image& out) {
            out.data = true;
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float)>("fx_emboss")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, c.number("strength", 4.0f));
        }));
    definitions.push_back(sourceToDest("image.oilpaint", "Oil Paint", "Artistic",
        "Kuwahara filter: flat, edge-keeping patches like brush strokes. Radius 2 to 8.",
        { intIn("radius", 4) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_kuwahara")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, juce::jlimit(1, 16, c.integer("radius", 4)));
        }));
    definitions.push_back(sourceToDest("image.halftone", "Halftone", "Artistic", "Black dots on white, sized by darkness, one per cell.",
        { intIn("cell", 8) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_halftone")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, juce::jmax(2, c.integer("cell", 8)));
        }));
    definitions.push_back(inPlace("image.dither", "Dither", "Artistic", "Ordered (Bayer) dither to a few levels per channel.",
        { intIn("levels", 2) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, std::int64_t, std::int64_t)>("fx_dither")(
                img.rgba.data(), img.width, img.height, juce::jmax(2, c.integer("levels", 2)));
        }));

    // Watercolour: Kuwahara smoothing, Sobel edge darkening, and fine + coarse noise for pigment granulation.
    definitions.push_back(define("image.watercolor", "Watercolor", "Artistic",
        "Smoothed colour with darkened edges and pigment granulation.",
        { imageIn("image"), intIn("smoothing", 3), floatIn("edges", 0.6f), floatIn("granulation", 0.25f), intIn("seed", 1) },
        { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const auto count = pixelCount(*input);
            auto smooth = blank(input->width, input->height, false);
            auto edges = blank(input->width, input->height, true);
            auto fine = blank(input->width, input->height, true);
            auto coarse = blank(input->width, input->height, true);
            const int seed = c.integer("seed", 1);
            auto& r = c.routines;
            if (! ok(r.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_kuwahara")(
                         input->rgba.data(), smooth->rgba.data(), input->width, input->height, juce::jlimit(1, 16, c.integer("smoothing", 3))), "Watercolor", error)
                || ! ok(r.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float)>("fx_edges")(
                            smooth->rgba.data(), edges->rgba.data(), input->width, input->height, 2.0f), "Watercolor", error)
                || ! ok(r.fx<std::int64_t (*)(float*, const float*, std::int64_t, float)>("fx_darken_edges")(
                            smooth->rgba.data(), edges->rgba.data(), count, c.number("edges", 0.6f)), "Watercolor", error)
                || ! ok(r.noise(fine->rgba.data(), input->width, input->height, 64, 3, seed), "Watercolor", error)
                || ! ok(r.noise(coarse->rgba.data(), input->width, input->height, 8, 3, seed + 17), "Watercolor", error))
                return false;
            for (size_t i = 0; i < fine->rgba.size(); ++i)
                fine->rgba[i] = 0.5f * (fine->rgba[i] + coarse->rgba[i]);
            if (! ok(r.fx<std::int64_t (*)(float*, const float*, std::int64_t, float)>("fx_granulate")(
                         smooth->rgba.data(), fine->rgba.data(), count, c.number("granulation", 0.25f)), "Watercolor", error))
                return false;
            out["image"] = smooth;
            return true;
        }));

    // Cartoon: posterized colour with dark outlines where edges are strong.
    definitions.push_back(define("image.cartoon", "Cartoon", "Artistic", "Flat colour bands with ink outlines.",
        { imageIn("image"), intIn("levels", 5), floatIn("ink", 1.5f) }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto image = copyOf(input);
            auto edges = blank(input->width, input->height, true);
            auto& r = c.routines;
            if (! ok(r.fx<std::int64_t (*)(float*, std::int64_t, std::int64_t)>("fx_posterize")(image->rgba.data(), pixelCount(*image), juce::jmax(2, c.integer("levels", 5))), "Cartoon", error)
                || ! ok(r.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float)>("fx_edges")(
                            input->rgba.data(), edges->rgba.data(), input->width, input->height, 1.0f), "Cartoon", error)
                || ! ok(r.fx<std::int64_t (*)(float*, const float*, std::int64_t, float)>("fx_darken_edges")(
                            image->rgba.data(), edges->rgba.data(), pixelCount(*image), c.number("ink", 1.5f)), "Cartoon", error))
                return false;
            out["image"] = image;
            return true;
        }));

    // Sketch: pencil lines - the edges, inverted to dark lines on white.
    definitions.push_back(sourceToDest("image.sketch", "Sketch", "Artistic", "Dark pencil lines where the image has edges.",
        { floatIn("strength", 2.0f) }, [](Context& c, const Image& in, Image& out) {
            out.data = false;
            if (c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float)>("fx_edges")(
                    in.rgba.data(), out.rgba.data(), in.width, in.height, c.number("strength", 2.0f)) != 1)
                return std::int64_t { 0 };
            return c.routines.invert(out.rgba.data(), pixelCount(out));
        }));

    // --- Edges, noise, light ---
    definitions.push_back(sourceToDest("image.edges", "Edge Detect", "Edges",
        "Edge strength (Sobel) as grey.", { floatIn("strength", 1.0f) }, [](Context& c, const Image& in, Image& out) {
            out.data = true;
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float)>("fx_edges")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, c.number("strength", 1.0f));
        }));

    auto blurCopy = [](Context& c, const Image& source, int radius, juce::String& error) -> std::shared_ptr<Image> {
        auto image = std::make_shared<Image>(source);
        radius = juce::jlimit(0, juce::jmax(0, (juce::jmin(image->width, image->height) - 1) / 2), radius);
        if (radius > 0)
            for (int ch = 0; ch < 4; ++ch)
            {
                auto plane = channel(*image, ch);
                std::vector<float> a, b;
                if (! c.surfaceMaps.boxBlur(plane, a, image->width, image->height, radius, error)
                    || ! c.surfaceMaps.boxBlur(a, b, image->width, image->height, radius, error)
                    || ! c.surfaceMaps.boxBlur(b, plane, image->width, image->height, radius, error))
                    return nullptr;
                setChannel(*image, ch, plane);
            }
        return image;
    };

    definitions.push_back(define("image.sharpen", "Sharpen", "Edges", "Unsharp mask: radius and amount.",
        { imageIn("image"), floatIn("radius", 2.0f), floatIn("amount", 1.0f) }, { imageOut("image") },
        [needInput, blurCopy](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto blurred = blurCopy(c, *input, juce::roundToInt(c.number("radius", 2.0f)), error);
            if (blurred == nullptr) return false;
            auto image = copyOf(input);
            if (! ok(c.routines.fx<std::int64_t (*)(float*, const float*, std::int64_t, float)>("fx_unsharp")(
                         image->rgba.data(), blurred->rgba.data(), pixelCount(*image), c.number("amount", 1.0f)), "Sharpen", error))
                return false;
            out["image"] = image;
            return true;
        }));

    definitions.push_back(inPlace("image.addnoise", "Add Noise", "Noise", "Adds noise of +/- amount; mono = the same in every channel.",
        { floatIn("amount", 0.1f), intIn("seed", 1), boolIn("mono", true) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, std::int64_t, float, std::int64_t, std::int64_t)>("fx_add_noise")(
                img.rgba.data(), img.width, img.height, c.number("amount", 0.1f), c.integer("seed", 1), c.flag("mono", true) ? 1 : 0);
        }));

    definitions.push_back(inPlace("image.vignette", "Vignette", "Light", "Darkens towards the corners; radius is where it starts (0 to 1).",
        { floatIn("strength", 0.6f), floatIn("radius", 0.4f) }, [](Context& c, Image& img) {
            return c.routines.fx<std::int64_t (*)(float*, std::int64_t, std::int64_t, float, float)>("fx_vignette")(
                img.rgba.data(), img.width, img.height, c.number("strength", 0.6f), juce::jlimit(0.0f, 0.99f, c.number("radius", 0.4f)));
        }));

    definitions.push_back(define("image.glow", "Glow", "Light", "A soft glow: a blurred copy screen-blended on top.",
        { imageIn("image"), floatIn("radius", 8.0f), floatIn("amount", 0.6f) }, { imageOut("image") },
        [needInput, blurCopy](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto blurred = blurCopy(c, *input, juce::roundToInt(c.number("radius", 8.0f)), error);
            if (blurred == nullptr) return false;
            auto image = copyOf(input);
            if (! ok(c.routines.fx<std::int64_t (*)(float*, const float*, std::int64_t, float)>("fx_glow")(
                         image->rgba.data(), blurred->rgba.data(), pixelCount(*image), c.number("amount", 0.6f)), "Glow", error))
                return false;
            out["image"] = image;
            return true;
        }));

    // --- Distort ---
    definitions.push_back(sourceToDest("image.swirl", "Swirl", "Distort", "Twists around the centre: angle in degrees, radius 0 to 1.",
        { floatIn("angle", 180.0f), floatIn("radius", 1.0f) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float, float)>("fx_swirl")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, c.number("angle", 180.0f) * 0.0174532925f, c.number("radius", 1.0f));
        }));
    definitions.push_back(sourceToDest("image.ripple", "Ripple", "Distort", "Sine-wave shift, horizontal or vertical.",
        { floatIn("amplitude", 8.0f), floatIn("wavelength", 64.0f), enumIn("direction", "Axis", 0) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float, float, std::int64_t)>("fx_ripple")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, c.number("amplitude", 8.0f), c.number("wavelength", 64.0f),
                juce::jlimit(0, 1, c.integer("direction", 0)));
        }));
    definitions.push_back(sourceToDest("image.kaleidoscope", "Kaleidoscope", "Distort", "Mirrored wedges around the centre.",
        { intIn("segments", 6) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_kaleidoscope")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, juce::jmax(1, c.integer("segments", 6)));
        }));
    definitions.push_back(sourceToDest("image.polar", "Polar Coordinates", "Distort", "Wraps the image around the centre, or unwraps it.",
        { boolIn("toPolar", true) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("fx_polar")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, c.flag("toPolar", true) ? 1 : 0);
        }));
    definitions.push_back(sourceToDest("image.offset", "Offset", "Distort",
        "Shifts the image, wrapping round - x and y are fractions of the size (0.5 = half). Shows tiling seams.",
        { floatIn("x", 0.5f), floatIn("y", 0.5f) }, [](Context& c, const Image& in, Image& out) {
            return c.routines.fx<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t)>("fx_offset")(
                in.rgba.data(), out.rgba.data(), in.width, in.height, juce::roundToInt(c.number("x", 0.5f) * static_cast<float>(in.width)),
                juce::roundToInt(c.number("y", 0.5f) * static_cast<float>(in.height)));
        }));

    // --- Output --- File > Render Outputs saves each Output node's image as a project image named by `name`.
    definitions.push_back(define("image.output", "Output", "Output",
        "A result of this graph. File > Render Outputs saves it as a project image called `name` (colour as 8-bit PNG, data such as "
        "height as 16-bit grey). Rendering again under the same name makes a new version.",
        { imageIn("image"), textIn("name") }, { imageOut("image") },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            out["image"] = input;
            return true;
        }));

    // --- Combine ---
    definitions.push_back(define("image.blend", "Blend", "Combine",
        "Foreground over background, combined by the blend mode.",
        { imageIn("background"), imageIn("foreground"), enumIn("mode", "BlendMode", 0), floatIn("opacity", 1.0f) }, { imageOut("image") },
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
    for (const auto& e : enums)
        registry.RegisterEnum(e);
    for (const auto& d : definitions)
        registry.Register(d.descriptor);
    ns::RegisterSymbolGetNodes(registry); // params / constants / variables (shared/NodeSystem/SYMBOLS.md)
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

bool Evaluator::evaluateValue(const ns::Graph& graph, ns::NodeId node, const std::string& output, ns::PinDefaultValue& value,
                              juce::String& error)
{
    if (! isReady())
    {
        error = getError();
        return false;
    }
    std::string signature;
    if (! evaluateNode(graph, node, { output }, signature, error, 0))
        return false;
    auto found = cache[node].values.find(output);
    if (found == cache[node].values.end())
    {
        error = "The node has no value output called " + juce::String(output) + ".";
        return false;
    }
    value = found->second;
    return true;
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
    // A symbol Get node: the param's outside value if one is set, else the symbol's own value.
    if (ns::IsSymbolGetNode(node->TypeName()))
    {
        const auto* symbol = ns::SymbolForGetNode(graph, *node);
        if (symbol == nullptr)
        {
            error = "A Get node's symbol is missing - choose one in its Properties.";
            return false;
        }
        auto value = symbol->value;
        if (symbol->kind == ns::SymbolKind::Param)
        {
            auto overridden = host.paramOverrides.find(symbol->id);
            if (overridden != host.paramOverrides.end())
                value = overridden->second;
        }
        signatureOut = std::to_string(std::hash<std::string> {}(node->TypeName() + "|" + symbol->id + "=" + valueText(value)));
        auto& cachedGet = cache[id];
        cachedGet.signature = signatureOut;
        cachedGet.outputs.clear();
        cachedGet.values = { { ns::kSymbolValuePin, value } };
        return true;
    }

    const auto* definition = library.find(node->TypeName());
    if (definition == nullptr)
    {
        error = "Unknown node type " + juce::String(node->TypeName()) + ".";
        return false;
    }

    // Wired image inputs first (each computes only the output it is asked for), building this node's signature.
    Context context { *node, host, *routines, *surfaceMaps, {}, {}, {}, {} };
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
        if (fromPin->type.dataType == ns::DataType::Texture)
            context.inputs[pin.name] = cache[wire->fromNode].outputs[fromPin->name];
        else
            context.wiredValues[pin.name] = cache[wire->fromNode].values[fromPin->name];
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
            complete = complete && (cached.outputs.count(name) > 0 || cached.values.count(name) > 0);
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
    cached.values = std::move(context.valueOutputs);
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
