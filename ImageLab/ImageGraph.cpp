#include "ImageGraph.h"
#include "ImageLabCore.h"
#include "ImageLabFrustSources.h"
#include "TextureSet.h"
#include "ImageDemoHost.h"
#include "DrawScript.h"

#include <creation/frust/PluginRuntime.h>
#include <node_system/frgraph_serialization.h>

#include <algorithm>
#include <array>
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

extern "C" float dr_host_floor(float v) { return std::floor(v); }
extern "C" float dr_host_sqrt(float v) { return std::sqrt(v < 0.0f ? 0.0f : v); }
extern "C" float dr_host_i64_to_f32(std::int64_t v) { return static_cast<float>(v); }
extern "C" std::int64_t dr_host_f32_to_i64(float v) { return static_cast<std::int64_t>(v); }
extern "C" float dr_host_sin(float v) { return std::sin(v); }
extern "C" float dr_host_cos(float v) { return std::cos(v); }
extern "C" float dr_host_hash(std::int64_t x, std::int64_t y, std::int64_t seed) { return ig_host_hash(x, y, seed); }

extern "C" float an_host_sin(float v) { return std::sin(v); }
extern "C" float an_host_cos(float v) { return std::cos(v); }
extern "C" float an_host_sqrt(float v) { return std::sqrt(v < 0.0f ? 0.0f : v); }
extern "C" float an_host_atan2(float y, float x) { return std::atan2(y, x); }
extern "C" float an_host_log(float v) { return std::log(v > 1.0e-30f ? v : 1.0e-30f); }
extern "C" float an_host_pow(float b, float e) { return std::pow(b, e); }
extern "C" float an_host_floor(float v) { return std::floor(v); }
extern "C" float an_host_i64_to_f32(std::int64_t v) { return static_cast<float>(v); }
extern "C" std::int64_t an_host_f32_to_i64(float v) { return static_cast<std::int64_t>(v); }

constexpr const char* key = "image_graph";
constexpr const char* analysisKey = "image_analysis";
constexpr const char* drawKey = "drawing";
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

        // Painting a Drawing with a Brush (drawing.frust).
        runtime.registerHostFunction("dr_host_floor", reinterpret_cast<void*>(&dr_host_floor));
        runtime.registerHostFunction("dr_host_sqrt", reinterpret_cast<void*>(&dr_host_sqrt));
        runtime.registerHostFunction("dr_host_i64_to_f32", reinterpret_cast<void*>(&dr_host_i64_to_f32));
        runtime.registerHostFunction("dr_host_f32_to_i64", reinterpret_cast<void*>(&dr_host_f32_to_i64));
        runtime.registerHostFunction("dr_host_sin", reinterpret_cast<void*>(&dr_host_sin));
        runtime.registerHostFunction("dr_host_cos", reinterpret_cast<void*>(&dr_host_cos));
        runtime.registerHostFunction("dr_host_hash", reinterpret_cast<void*>(&dr_host_hash));
        ::frust::CompileRequest draw;
        draw.sources.push_back({ "drawing.frust", std::string(ImageLabFrust::drawing_frust, ImageLabFrust::drawing_frustSize) });
        if (! runtime.loadSource(drawKey, draw, loadError))
        {
            error = "Drawing FRust routines did not compile: " + juce::String(loadError);
            return;
        }
        stamps = reinterpret_cast<decltype(stamps)>(runtime.getFunction(drawKey, "dr_stamps"));
        fillShapes = reinterpret_cast<decltype(fillShapes)>(runtime.getFunction(drawKey, "dr_fill"));
        composite = reinterpret_cast<decltype(composite)>(runtime.getFunction(drawKey, "dr_composite"));
        contour = reinterpret_cast<decltype(contour)>(runtime.getFunction(drawKey, "dr_contour"));
        sample = reinterpret_cast<decltype(sample)>(runtime.getFunction(drawKey, "dr_sample"));
        if (! (stamps && fillShapes && composite && contour && sample))
        {
            error = "Drawing FRust routines are incomplete.";
            return;
        }

        // Image analysis (analysis.frust - to become the frust_image_analysis pod).
        for (auto [name, fn] : { std::pair { "an_host_sin", reinterpret_cast<void*>(&an_host_sin) },
                                 std::pair { "an_host_cos", reinterpret_cast<void*>(&an_host_cos) },
                                 std::pair { "an_host_sqrt", reinterpret_cast<void*>(&an_host_sqrt) },
                                 std::pair { "an_host_atan2", reinterpret_cast<void*>(&an_host_atan2) },
                                 std::pair { "an_host_log", reinterpret_cast<void*>(&an_host_log) },
                                 std::pair { "an_host_pow", reinterpret_cast<void*>(&an_host_pow) },
                                 std::pair { "an_host_floor", reinterpret_cast<void*>(&an_host_floor) },
                                 std::pair { "an_host_i64_to_f32", reinterpret_cast<void*>(&an_host_i64_to_f32) },
                                 std::pair { "an_host_f32_to_i64", reinterpret_cast<void*>(&an_host_f32_to_i64) } })
            runtime.registerHostFunction(name, fn);
        ::frust::CompileRequest analysis;
        analysis.sources.push_back({ "analysis.frust", std::string(ImageLabFrust::analysis_frust, ImageLabFrust::analysis_frustSize) });
        if (! runtime.loadSource(analysisKey, analysis, loadError))
        {
            error = "Image analysis FRust routines did not compile: " + juce::String(loadError);
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


    // An image-analysis routine by name (analysis.frust).
    template <typename Fn>
    Fn an(const char* name)
    {
        return reinterpret_cast<Fn>(runtime.getFunction(analysisKey, name));
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
    // drawing.frust
    std::int64_t (*stamps)(float*, std::int64_t, std::int64_t, const float*, std::int64_t, std::int64_t, float, const float*, std::int64_t,
                           std::int64_t, std::int64_t) = nullptr;
    std::int64_t (*fillShapes)(float*, std::int64_t, std::int64_t, const float*, std::int64_t, float*, float*) = nullptr;
    std::int64_t (*composite)(float*, const float*, std::int64_t, float, float, float, float) = nullptr;
    std::int64_t (*contour)(const float*, std::int64_t, std::int64_t, float, float*, std::int64_t) = nullptr;
    std::int64_t (*sample)(const float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t, std::int64_t, float*, std::int64_t) = nullptr;
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
ns::PinSignature boolOut(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Bool }, {} }; }
ns::PinSignature floatOut(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Float }, {} }; }
ns::PinSignature drawingIn(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Drawing }, {} }; }
ns::PinSignature drawingOut(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Drawing }, {} }; }
ns::PinSignature brushIn(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Brush }, {} }; }
ns::PinSignature brushOut(const std::string& name) { return { name, { ns::PinKind::Data, ns::DataType::Brush }, {} }; }
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
    d.descriptor.diagramTypes = { kImageDiagram };
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

std::map<std::string, double> Context::numericVariables() const
{
    std::map<std::string, double> values;
    if (graph == nullptr)
        return values;
    for (const auto& symbol : graph->Symbols())
    {
        auto value = symbol.value;
        if (symbol.kind == ns::SymbolKind::Param)
        {
            auto overridden = host.paramOverrides.find(symbol.id);
            if (overridden != host.paramOverrides.end())
                value = overridden->second;
        }
        if (const auto* f = std::get_if<float>(&value)) values[symbol.id] = *f;
        else if (const auto* i = std::get_if<std::int64_t>(&value)) values[symbol.id] = static_cast<double>(*i);
        else if (const auto* b = std::get_if<bool>(&value)) values[symbol.id] = *b ? 1.0 : 0.0;
    }
    return values;
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
    enums.push_back({ "BrushTip", "Brush Tip", { "Round", "Square", "Chalk", "Bristle", "Image" },
                      "The shape a brush stamps: Chalk is round, broken by a grain fixed to the canvas; Bristle is a row of "
                      "small bristles that streak; Image uses the image wired into tipImage." });
    enums.push_back({ "BrushRotation", "Brush Rotation", { "Fixed", "Follow Stroke", "Random" },
                      "How a brush's tip is turned: by its angle only, along the stroke's direction, or at random." });
    enums.push_back({ "CompareOp", "Comparison", { "Less Than", "Less or Equal", "Equal", "Greater or Equal", "Greater Than", "Not Equal" },
                      "How Compare relates a to b." });
    enums.push_back({ "LogicOp", "Logic", { "And", "Or", "Xor" }, "How Logic combines a and b." });
    enums.push_back({ "MathOp", "Math", { "Add", "Subtract", "Multiply", "Divide", "Minimum", "Maximum", "Power", "Modulo" },
                      "What Math does with a and b." });
    enums.push_back({ "PaintMode", "Paint Mode", { "Stroke", "Fill", "Fill and Stroke" },
                      "Run the brush along the drawing's lines, fill its shapes, or both." });

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

    // Conditions for decisions (shared/NodeSystem/FLOW.md): wire a result into a Switch or Route's selector.
    definitions.push_back(define("image.value.compare", "Compare", "Values", "a compared with b, on or off - for example to drive a Switch.",
        { floatIn("a", 0.0f), floatIn("b", 0.5f), enumIn("op", "CompareOp", 0) }, { boolOut("result") },
        [](Context& c, auto&, juce::String&) {
            const float a = c.number("a", 0.0f), b = c.number("b", 0.5f);
            bool r = false;
            switch (c.integer("op", 0))
            {
                case 0: r = a < b; break;
                case 1: r = a <= b; break;
                case 2: r = std::abs(a - b) < 1.0e-6f; break;
                case 3: r = a >= b; break;
                case 4: r = a > b; break;
                default: r = std::abs(a - b) >= 1.0e-6f; break;
            }
            c.valueOutputs["result"] = r;
            return true;
        }));
    definitions.push_back(define("image.value.logic", "Logic", "Values", "a and / or / xor b.",
        { boolIn("a", false), boolIn("b", false), enumIn("op", "LogicOp", 0) }, { boolOut("result") },
        [](Context& c, auto&, juce::String&) {
            const bool a = c.flag("a", false), b = c.flag("b", false);
            const int op = c.integer("op", 0);
            c.valueOutputs["result"] = op == 0 ? (a && b) : op == 1 ? (a || b) : (a != b);
            return true;
        }));
    definitions.push_back(define("image.value.not", "Not", "Values", "On becomes off, off becomes on.",
        { boolIn("a", false) }, { boolOut("result") },
        [](Context& c, auto&, juce::String&) {
            c.valueOutputs["result"] = ! c.flag("a", false);
            return true;
        }));
    definitions.push_back(define("image.value.math", "Math", "Values", "a and b combined into one number.",
        { floatIn("a", 0.0f), floatIn("b", 1.0f), enumIn("op", "MathOp", 0) }, { floatOut("result") },
        [](Context& c, auto&, juce::String& error) {
            const float a = c.number("a", 0.0f), b = c.number("b", 1.0f);
            float r = 0.0f;
            switch (c.integer("op", 0))
            {
                case 0: r = a + b; break;
                case 1: r = a - b; break;
                case 2: r = a * b; break;
                case 3:
                    if (b == 0.0f) { error = "Division by zero."; return false; }
                    r = a / b;
                    break;
                case 4: r = std::min(a, b); break;
                case 5: r = std::max(a, b); break;
                case 6: r = std::pow(a, b); break;
                default:
                    if (b == 0.0f) { error = "Modulo by zero."; return false; }
                    r = std::fmod(a, b);
                    break;
            }
            c.valueOutputs["result"] = r;
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
    {
        auto input = define("image.input", "Graph Input", "Output",
            "An image this graph takes in when another graph uses it as a node (a Graph node): the image wired into the Graph "
            "node's input called `name`. On its own, the graph uses the image wired or chosen here instead.",
            { textIn("name"), imageIn("image") }, { imageOut("image") },
            [needInput](Context& c, auto& out, juce::String& error) {
                auto given = c.host.graphInputs.find(c.text("name").replaceCharacter(' ', '_').toStdString());
                if (given != c.host.graphInputs.end() && given->second != nullptr)
                {
                    out["image"] = given->second;
                    return true;
                }
                auto own = needInput(c, "image", error);
                if (own == nullptr) return false;
                out["image"] = own;
                return true;
            });
        input.descriptor.graphPort = ns::GraphPort::input;
        input.readsGraphInputs = true;
        definitions.push_back(std::move(input));
    }
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
    definitions.back().descriptor.graphPort = ns::GraphPort::output; // an output of the graph when it is used as a node

    // --- Analysis --- measuring an image: colour channels, frequencies (Fourier), local detail, the colour spectrum.
    // The maths is the general analysis.frust pack; these nodes call it.
    auto greyImage = [](int w, int h) {
        auto image = std::make_shared<Image>();
        image->width = w;
        image->height = h;
        image->rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0.0f);
        image->data = true;
        return image;
    };
    // Split: one grey map per channel of a colour model; only the wanted ones are made.
    auto splitNode = [needInput, greyImage](std::string type, std::string name, std::string description, std::int64_t mode,
                                           std::vector<std::string> channels) {
        std::vector<ns::PinSignature> outs;
        for (const auto& ch : channels)
            outs.push_back(imageOut(ch));
        return define(std::move(type), std::move(name), "Analysis", std::move(description), { imageIn("image") }, outs,
            [needInput, greyImage, mode, channels](Context& c, auto& out, juce::String& error) {
                auto input = needInput(c, "image", error);
                if (input == nullptr) return false;
                auto channel = c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t)>("an_channel");
                for (size_t i = 0; i < channels.size(); ++i)
                    if (c.wants(channels[i]))
                    {
                        auto map = greyImage(input->width, input->height);
                        if (! ok(channel(input->rgba.data(), map->rgba.data(), pixelCount(*map), mode, static_cast<std::int64_t>(i)), "Split", error))
                            return false;
                        out[channels[i]] = map;
                    }
                return true;
            });
    };
    definitions.push_back(splitNode("image.analysis.split_rgb", "Split RGB", "The red, green, blue and alpha channels as grey maps.", 0,
                                    { "r", "g", "b", "a" }));
    definitions.push_back(splitNode("image.analysis.split_cmyk", "Split CMYK", "Cyan, magenta, yellow and black - the print separations.", 1,
                                    { "c", "m", "y", "k" }));
    definitions.push_back(splitNode("image.analysis.split_lab", "Split Lab",
                                    "Lightness (0..1) and the colour axes a (green-red) and b (blue-yellow), 0.5 = neutral. "
                                    "Detail usually lives in lightness, colour in broad patches.", 2, { "l", "a", "b" }));
    definitions.push_back(splitNode("image.analysis.split_hsv", "Split HSV", "Hue (0..1 around the wheel), saturation and value.", 3,
                                    { "h", "s", "v" }));

    // Combine: grey maps back into colour; a channel not wired takes its neutral value.
    auto combineNode = [greyImage](std::string type, std::string name, std::string description, std::int64_t mode,
                                   std::vector<std::string> channels, std::vector<float> neutral) {
        std::vector<ns::PinSignature> ins;
        for (const auto& ch : channels)
            ins.push_back(imageIn(ch));
        return define(std::move(type), std::move(name), "Analysis", std::move(description), ins, { imageOut("image") },
            [greyImage, mode, channels, neutral](Context& c, auto& out, juce::String& error) {
                int w = 0, h = 0;
                for (const auto& ch : channels)
                    if (auto in = c.inputs.find(ch); in != c.inputs.end() && in->second != nullptr)
                    {
                        if (w != 0 && (in->second->width != w || in->second->height != h))
                        {
                            error = "The channels must all be the same size.";
                            return false;
                        }
                        w = in->second->width;
                        h = in->second->height;
                    }
                if (w == 0)
                {
                    error = "Wire at least one channel in.";
                    return false;
                }
                std::vector<std::shared_ptr<Image>> fills;
                std::vector<const float*> parts;
                for (size_t i = 0; i < 4; ++i)
                {
                    auto in = i < channels.size() ? c.inputs.find(channels[i]) : c.inputs.end();
                    if (in != c.inputs.end() && in->second != nullptr)
                        parts.push_back(in->second->rgba.data());
                    else
                    {
                        auto fill = greyImage(w, h);
                        std::fill(fill->rgba.begin(), fill->rgba.end(), i < neutral.size() ? neutral[i] : 1.0f);
                        fills.push_back(fill);
                        parts.push_back(fill->rgba.data());
                    }
                }
                auto image = greyImage(w, h);
                image->data = false;
                auto combine = c.routines.an<std::int64_t (*)(const float*, const float*, const float*, const float*, float*, std::int64_t,
                                                              std::int64_t)>("an_combine");
                if (! ok(combine(parts[0], parts[1], parts[2], parts[3], image->rgba.data(), pixelCount(*image), mode), "Combine", error))
                    return false;
                out["image"] = image;
                return true;
            });
    };
    definitions.push_back(combineNode("image.analysis.combine_rgb", "Combine RGB", "Red, green, blue and alpha maps into an image.", 0,
                                      { "r", "g", "b", "a" }, { 0.0f, 0.0f, 0.0f, 1.0f }));
    definitions.push_back(combineNode("image.analysis.combine_cmyk", "Combine CMYK", "Cyan, magenta, yellow and black into an image.", 1,
                                      { "c", "m", "y", "k" }, { 0.0f, 0.0f, 0.0f, 0.0f }));
    definitions.push_back(combineNode("image.analysis.combine_lab", "Combine Lab", "Lightness and the a / b colour axes into an image.", 2,
                                      { "l", "a", "b" }, { 0.5f, 0.5f, 0.5f, 1.0f }));
    definitions.push_back(combineNode("image.analysis.combine_hsv", "Combine HSV", "Hue, saturation and value into an image.", 3,
                                      { "h", "s", "v" }, { 0.0f, 0.0f, 1.0f, 1.0f }));

    definitions.push_back(define("image.analysis.colour_mask", "Colour Mask", "Analysis",
        "How much of a chosen colour each pixel holds: 1 within the tolerance of it, fading to 0 over the softness.",
        { imageIn("image"), colourIn("color", 0.8f, 0.4f, 0.1f), floatIn("tolerance", 0.1f), floatIn("softness", 0.1f) }, { imageOut("mask") },
        [needInput, greyImage](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const auto colour = c.colour("color", { 0.8f, 0.4f, 0.1f });
            auto mask = greyImage(input->width, input->height);
            auto fn = c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, float, float, float, float, float)>("an_colour_mask");
            if (! ok(fn(input->rgba.data(), mask->rgba.data(), pixelCount(*mask), colour.x, colour.y, colour.z, c.number("tolerance", 0.1f),
                        c.number("softness", 0.1f)), "Colour Mask", error))
                return false;
            out["mask"] = mask;
            return true;
        }));
    definitions.push_back(define("image.analysis.hue_band", "Hue Band", "Analysis",
        "The colours in a range of hues (degrees around the wheel: 0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta), "
        "weighted by how saturated they are.",
        { imageIn("image"), floatIn("hue", 30.0f), floatIn("width", 40.0f), floatIn("softness", 20.0f) }, { imageOut("mask") },
        [needInput, greyImage](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            auto mask = greyImage(input->width, input->height);
            auto fn = c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, float, float, float)>("an_hue_band");
            if (! ok(fn(input->rgba.data(), mask->rgba.data(), pixelCount(*mask), c.number("hue", 30.0f) / 360.0f,
                        c.number("width", 40.0f) / 360.0f, c.number("softness", 20.0f) / 360.0f), "Hue Band", error))
                return false;
            out["mask"] = mask;
            return true;
        }));

    // Fourier: the FFT needs power-of-two sizes, so an image that is not is resampled (wrapping, so tiling holds)
    // to the nearest, worked on, and resampled back.
    auto toPow2 = [](int n) {
        int p = 2;
        while (p < 4096 && p * 2 - n < n - p) // nearest power of two
            p *= 2;
        return juce::jlimit(2, 4096, p);
    };
    auto resampled = [](Context& c, const Image& from, int w, int h) {
        auto image = std::make_shared<Image>();
        image->width = w;
        image->height = h;
        image->data = from.data;
        image->rgba.assign(static_cast<size_t>(w) * static_cast<size_t>(h) * 4, 0.0f);
        c.routines.an<std::int64_t (*)(const float*, std::int64_t, std::int64_t, float*, std::int64_t, std::int64_t)>("an_resample")(
            from.rgba.data(), from.width, from.height, image->rgba.data(), w, h);
        return image;
    };
    definitions.push_back(define("image.analysis.frequency_band", "Frequency Band", "Analysis",
        "Keeps only the detail between two sizes (fractions of the image width): coarse shapes, fine grain, or one pattern's "
        "scale. Softness is in octaves; Keep Average keeps the overall brightness. The image is treated as repeating, so a "
        "tileable image stays tileable.",
        { imageIn("image"), floatIn("smallest", 0.02f), floatIn("largest", 0.1f), floatIn("softness", 0.5f), boolIn("keepAverage", true) },
        { imageOut("image") },
        [needInput, toPow2, resampled](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const int w = toPow2(input->width), h = toPow2(input->height);
            auto work = (w == input->width && h == input->height) ? copyOf(input) : resampled(c, *input, w, h);
            std::vector<float> complexBuffer(static_cast<size_t>(w) * static_cast<size_t>(h) * 2);
            auto pack = c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t)>("an_pack");
            auto fft = c.routines.an<std::int64_t (*)(float*, std::int64_t, std::int64_t, std::int64_t)>("an_fft2d");
            auto band = c.routines.an<std::int64_t (*)(float*, std::int64_t, std::int64_t, float, float, float, std::int64_t)>("an_band");
            auto unpack = c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t)>("an_unpack");
            const float smallest = juce::jmax(1.0e-4f, c.number("smallest", 0.02f)) * static_cast<float>(w);
            const float largest = juce::jmax(1.0e-4f, c.number("largest", 0.1f)) * static_cast<float>(w);
            for (std::int64_t channel = 0; channel < 3; ++channel)
            {
                pack(work->rgba.data(), complexBuffer.data(), pixelCount(*work), channel);
                fft(complexBuffer.data(), w, h, 0);
                band(complexBuffer.data(), w, h, smallest, largest, juce::jmax(0.0f, c.number("softness", 0.5f)), c.flag("keepAverage", true) ? 1 : 0);
                fft(complexBuffer.data(), w, h, 1);
                unpack(complexBuffer.data(), work->rgba.data(), pixelCount(*work), channel);
            }
            out["image"] = (w == input->width && h == input->height) ? work : resampled(c, *work, input->width, input->height);
            return true;
        }));
    definitions.push_back(define("image.analysis.spectrum", "Spectrum", "Analysis",
        "The image's frequency picture (brightness): the average in the middle, fine detail towards the edges, the direction "
        "of a pattern as a line of bright points across it.",
        { imageIn("image") }, { imageOut("spectrum") },
        [needInput, toPow2, resampled, greyImage](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const int w = toPow2(input->width), h = toPow2(input->height);
            auto work = (w == input->width && h == input->height) ? input : ImagePtr(resampled(c, *input, w, h));
            std::vector<float> complexBuffer(static_cast<size_t>(w) * static_cast<size_t>(h) * 2);
            c.routines.an<std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t)>("an_pack")(work->rgba.data(), complexBuffer.data(),
                                                                                                         pixelCount(*work), 4);
            c.routines.an<std::int64_t (*)(float*, std::int64_t, std::int64_t, std::int64_t)>("an_fft2d")(complexBuffer.data(), w, h, 0);
            auto spectrum = greyImage(w, h);
            c.routines.an<std::int64_t (*)(float*, float*, std::int64_t, std::int64_t)>("an_spectrum")(complexBuffer.data(), spectrum->rgba.data(), w, h);
            out["spectrum"] = spectrum;
            return true;
        }));
    definitions.push_back(define("image.analysis.detail_map", "Detail Map", "Analysis",
        "How much change each part of the image holds: the brightness spread in a square (a fraction of the width) around each "
        "pixel. Busy areas are bright, flat ones dark.",
        { imageIn("image"), floatIn("square", 0.02f) }, { imageOut("detail") },
        [needInput, greyImage](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const int radius = juce::jlimit(1, 256, juce::roundToInt(c.number("square", 0.02f) * static_cast<float>(input->width) * 0.5f));
            std::vector<float> a(static_cast<size_t>(pixelCount(*input))), b(a.size());
            auto detail = greyImage(input->width, input->height);
            c.routines.an<std::int64_t (*)(const float*, std::int64_t, std::int64_t, std::int64_t, float*, float*, float*)>("an_detail")(
                input->rgba.data(), input->width, input->height, radius, a.data(), b.data(), detail->rgba.data());
            out["detail"] = detail;
            return true;
        }));

    // Local frequency: a windowed FFT square by square (squares half-overlapping), then spread over the image.
    struct LocalAnalysis
    {
        int win = 0, hop = 0, gw = 0, gh = 0;
        std::vector<float> scale, direction, strength, bands;
    };
    auto analyseLocally = [](Context& c, const Image& input, float square) {
        LocalAnalysis local;
        int win = 8;
        const float target = square * static_cast<float>(input.width);
        while (win < 256 && static_cast<float>(win * 2) <= target * 1.414f)
            win *= 2;
        local.win = win;
        local.hop = win / 2;
        local.gw = (input.width + local.hop - 1) / local.hop;
        local.gh = (input.height + local.hop - 1) / local.hop;
        const auto cells = static_cast<size_t>(local.gw) * static_cast<size_t>(local.gh);
        local.scale.assign(cells, 0.0f);
        local.direction.assign(cells, 0.0f);
        local.strength.assign(cells, 0.0f);
        local.bands.assign(cells * 7, 0.0f);
        std::vector<float> scratch(static_cast<size_t>(win) * static_cast<size_t>(win) * 2);
        c.routines.an<std::int64_t (*)(const float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t, float*, std::int64_t, std::int64_t,
                                       float*, float*, float*, float*)>("an_local")(
            input.rgba.data(), input.width, input.height, win, local.hop, scratch.data(), local.gw, local.gh, local.scale.data(),
            local.direction.data(), local.strength.data(), local.bands.data());
        return local;
    };
    auto gridImage = [greyImage](Context& c, const LocalAnalysis& local, const std::vector<float>& grid, const Image& like) {
        auto map = greyImage(like.width, like.height);
        c.routines.an<std::int64_t (*)(const float*, std::int64_t, std::int64_t, std::int64_t, std::int64_t, float*, std::int64_t, std::int64_t)>(
            "an_grid_to_image")(grid.data(), local.gw, local.gh, local.win, local.hop, map->rgba.data(), like.width, like.height);
        return map;
    };
    definitions.push_back(define("image.analysis.local_frequency", "Local Frequency", "Analysis",
        "Looks at the image square by square (a Fourier transform of each): Scale is the typical size of the detail there (a "
        "fraction of the square), Direction which way its lines run (0 horizontal, 0.5 vertical, 1 horizontal again), Strength "
        "how much it runs one way.",
        { imageIn("image"), floatIn("square", 0.0625f) }, { imageOut("scale"), imageOut("direction"), imageOut("strength") },
        [needInput, analyseLocally, gridImage](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const auto local = analyseLocally(c, *input, c.number("square", 0.0625f));
            if (c.wants("scale")) out["scale"] = gridImage(c, local, local.scale, *input);
            if (c.wants("direction")) out["direction"] = gridImage(c, local, local.direction, *input);
            if (c.wants("strength")) out["strength"] = gridImage(c, local, local.strength, *input);
            return true;
        }));
    {
        auto evenness = define("image.analysis.evenness", "Evenness", "Analysis",
            "Whether the image is the same kind of thing all over - what a texture needs to tile without patterning out. "
            "Compares each square's frequency make-up and contrast with the whole: the map is bright where a square differs, "
            "Evenness is 1 for perfectly even.",
            { imageIn("image"), floatIn("square", 0.125f) }, { imageOut("map"), floatOut("evenness") },
            [needInput, analyseLocally, gridImage](Context& c, auto& out, juce::String& error) {
                auto input = needInput(c, "image", error);
                if (input == nullptr) return false;
                const auto local = analyseLocally(c, *input, c.number("square", 0.125f));
                const size_t cells = local.scale.size();
                // Each square: six band fractions and its energy against the mean energy.
                std::array<double, 6> meanBands {};
                double meanEnergy = 0.0;
                for (size_t i = 0; i < cells; ++i)
                {
                    for (size_t b = 0; b < 6; ++b)
                        meanBands[b] += local.bands[i * 7 + b];
                    meanEnergy += local.bands[i * 7 + 6];
                }
                for (auto& m : meanBands)
                    m /= static_cast<double>(cells);
                meanEnergy /= static_cast<double>(cells);
                std::vector<float> distance(cells, 0.0f);
                double total = 0.0;
                for (size_t i = 0; i < cells; ++i)
                {
                    double d = 0.0;
                    for (size_t b = 0; b < 6; ++b)
                        d += std::abs(local.bands[i * 7 + b] - meanBands[b]);
                    const double energy = meanEnergy > 0.0 ? local.bands[i * 7 + 6] / meanEnergy : 1.0;
                    d = 0.5 * d + 0.5 * std::min(1.0, std::abs(energy - 1.0));
                    distance[i] = static_cast<float>(std::min(1.0, d));
                    total += distance[i];
                }
                if (c.wants("map")) out["map"] = gridImage(c, local, distance, *input);
                c.valueOutputs["evenness"] = static_cast<float>(1.0 - total / static_cast<double>(cells));
                return true;
            });
        definitions.push_back(std::move(evenness));
    }
    definitions.push_back(define("image.analysis.colour_spectrum", "Colour Spectrum", "Analysis",
        "The image's colours around the hue wheel: Chart shows how much of each hue it holds; Dominant 1-3 are its main hues; "
        "Harmony is the shape of its palette from a Fourier analysis of the hues - 1 one main hue, 2 complementary, 3 a triad, "
        "4 a tetrad, 0 an even spread or no colour.",
        { imageIn("image") }, { imageOut("chart"), { "dominant1", { ns::PinKind::Data, ns::DataType::Color }, {} },
                                { "dominant2", { ns::PinKind::Data, ns::DataType::Color }, {} },
                                { "dominant3", { ns::PinKind::Data, ns::DataType::Color }, {} },
                                { "harmony", { ns::PinKind::Data, ns::DataType::Int }, {} } },
        [needInput](Context& c, auto& out, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            constexpr int bins = 36;
            std::vector<float> hist(bins);
            c.routines.an<std::int64_t (*)(const float*, std::int64_t, float*, std::int64_t)>("an_hue_histogram")(input->rgba.data(), pixelCount(*input),
                                                                                                                hist.data(), bins);
            if (c.wants("chart"))
            {
                auto chart = std::make_shared<Image>();
                chart->width = 360;
                chart->height = 120;
                chart->rgba.assign(360 * 120 * 4, 0.0f);
                c.routines.an<std::int64_t (*)(const float*, std::int64_t, float*, std::int64_t, std::int64_t)>("an_hue_chart")(hist.data(), bins,
                                                                                                                             chart->rgba.data(), 360, 120);
                out["chart"] = chart;
            }
            // Harmony: the strongest of the first four circular harmonics of the hue histogram.
            double sum = 0.0;
            for (float v : hist) sum += v;
            int harmony = 0;
            double best = 0.0;
            for (int k = 1; k <= 4 && sum > 0.0; ++k)
            {
                double re = 0.0, im = 0.0;
                for (int j = 0; j < bins; ++j)
                {
                    const double a = 2.0 * juce::MathConstants<double>::pi * k * j / bins;
                    re += hist[static_cast<size_t>(j)] * std::cos(a);
                    im -= hist[static_cast<size_t>(j)] * std::sin(a);
                }
                const double magnitude = std::sqrt(re * re + im * im) / sum;
                if (magnitude > best + 1.0e-6)
                {
                    best = magnitude;
                    harmony = k;
                }
            }
            if (best < 0.15)
                harmony = 0;
            c.valueOutputs["harmony"] = static_cast<std::int64_t>(harmony);
            // Dominant hues: the highest peaks of the (lightly smoothed) histogram, at least 30 degrees apart.
            std::vector<float> smooth(bins);
            float top = 0.0f;
            for (int j = 0; j < bins; ++j)
            {
                smooth[static_cast<size_t>(j)] = 0.25f * hist[static_cast<size_t>((j + bins - 1) % bins)] + 0.5f * hist[static_cast<size_t>(j)]
                                               + 0.25f * hist[static_cast<size_t>((j + 1) % bins)];
                top = std::max(top, smooth[static_cast<size_t>(j)]);
            }
            std::vector<int> peaks;
            for (int pick = 0; pick < 3; ++pick)
            {
                int bestBin = -1;
                for (int j = 0; j < bins; ++j)
                {
                    bool tooClose = false;
                    for (int p : peaks)
                        tooClose = tooClose || std::min((j - p + bins) % bins, (p - j + bins) % bins) < 3;
                    if (! tooClose && smooth[static_cast<size_t>(j)] > 0.05f * top && (bestBin < 0 || smooth[static_cast<size_t>(j)] > smooth[static_cast<size_t>(bestBin)]))
                        bestBin = j;
                }
                if (bestBin >= 0)
                    peaks.push_back(bestBin);
            }
            const char* names[3] = { "dominant1", "dominant2", "dominant3" };
            for (int i = 0; i < 3; ++i)
            {
                ns::Vec3Default colour { 0.0f, 0.0f, 0.0f };
                if (i < static_cast<int>(peaks.size()))
                {
                    const auto hue = juce::Colour::fromHSV((static_cast<float>(peaks[static_cast<size_t>(i)]) + 0.5f) / bins, 1.0f, 1.0f, 1.0f);
                    colour = { hue.getFloatRed(), hue.getFloatGreen(), hue.getFloatBlue() };
                }
                c.valueOutputs[names[i]] = colour;
            }
            return true;
        }));

    // --- Draw --- (section 5) shapes make a Drawing; Brush says how it is painted; Paint makes the image.
    // Positions are 0..1 across the canvas, (0, 0) top-left.
    auto shapeNode = [](std::string type, std::string name, std::string description, std::vector<ns::PinSignature> inputs,
                        std::function<drawing::Path(Context&)> make) {
        return define(std::move(type), std::move(name), "Draw", std::move(description), std::move(inputs), { drawingOut("drawing") },
            [make](Context& c, auto&, juce::String&) {
                auto result = std::make_shared<drawing::Drawing>();
                result->paths.push_back(make(c));
                c.drawingOutputs["drawing"] = result;
                return true;
            });
    };
    definitions.push_back(shapeNode("draw.line", "Line", "A straight line between two points.",
        { floatIn("x1", 0.1f), floatIn("y1", 0.5f), floatIn("x2", 0.9f), floatIn("y2", 0.5f) },
        [](Context& c) { return drawing::line({ c.number("x1", 0.1f), c.number("y1", 0.5f) }, { c.number("x2", 0.9f), c.number("y2", 0.5f) }); }));
    definitions.push_back(shapeNode("draw.rectangle", "Rectangle", "A rectangle from its top-left corner.",
        { floatIn("x", 0.25f), floatIn("y", 0.25f), floatIn("width", 0.5f), floatIn("height", 0.5f) },
        [](Context& c) { return drawing::rectangle(c.number("x", 0.25f), c.number("y", 0.25f), c.number("width", 0.5f), c.number("height", 0.5f)); }));
    definitions.push_back(shapeNode("draw.circle", "Circle", "A circle around a centre point.",
        { floatIn("x", 0.5f), floatIn("y", 0.5f), floatIn("radius", 0.25f) },
        [](Context& c) {
            const float r = c.number("radius", 0.25f);
            return drawing::ellipse({ c.number("x", 0.5f), c.number("y", 0.5f) }, r, r);
        }));
    definitions.push_back(shapeNode("draw.polygon", "Polygon", "A regular polygon: 3 sides is a triangle, 6 a hexagon. Rotation 0 puts a corner at the top.",
        { floatIn("x", 0.5f), floatIn("y", 0.5f), floatIn("radius", 0.25f), intIn("sides", 6), floatIn("rotation", 0.0f) },
        [](Context& c) {
            return drawing::polygon({ c.number("x", 0.5f), c.number("y", 0.5f) }, c.number("radius", 0.25f), juce::jlimit(3, 1000, c.integer("sides", 6)),
                                    c.number("rotation", 0.0f));
        }));

    definitions.push_back(shapeNode("draw.star", "Star", "A star with a point straight up at rotation 0.",
        { floatIn("x", 0.5f), floatIn("y", 0.5f), floatIn("outerRadius", 0.3f), floatIn("innerRadius", 0.12f), intIn("points", 5),
          floatIn("rotation", 0.0f) },
        [](Context& c) {
            return drawing::star({ c.number("x", 0.5f), c.number("y", 0.5f) }, c.number("outerRadius", 0.3f), c.number("innerRadius", 0.12f),
                                 juce::jlimit(2, 1000, c.integer("points", 5)), c.number("rotation", 0.0f));
        }));
    definitions.push_back(shapeNode("draw.arc", "Arc", "Part of a circle: from the start angle, through the sweep (degrees, clockwise, 0 pointing right).",
        { floatIn("x", 0.5f), floatIn("y", 0.5f), floatIn("radius", 0.25f), floatIn("start", 0.0f), floatIn("sweep", 180.0f) },
        [](Context& c) {
            return drawing::arc({ c.number("x", 0.5f), c.number("y", 0.5f) }, c.number("radius", 0.25f), c.number("start", 0.0f), c.number("sweep", 180.0f));
        }));
    definitions.push_back(shapeNode("draw.bezier", "Curve", "A smooth curve from (x1, y1) to (x2, y2), pulled towards two control points.",
        { floatIn("x1", 0.1f), floatIn("y1", 0.7f), floatIn("cx1", 0.3f), floatIn("cy1", 0.1f), floatIn("cx2", 0.7f), floatIn("cy2", 0.9f),
          floatIn("x2", 0.9f), floatIn("y2", 0.3f) },
        [](Context& c) {
            return drawing::bezier({ c.number("x1", 0.1f), c.number("y1", 0.7f) }, { c.number("cx1", 0.3f), c.number("cy1", 0.1f) },
                                   { c.number("cx2", 0.7f), c.number("cy2", 0.9f) }, { c.number("x2", 0.9f), c.number("y2", 0.3f) });
        }));
    definitions.push_back(shapeNode("draw.spiral", "Spiral", "A spiral out from the inner radius to the outer one, starting to the right.",
        { floatIn("x", 0.5f), floatIn("y", 0.5f), floatIn("innerRadius", 0.0f), floatIn("outerRadius", 0.4f), floatIn("turns", 4.0f) },
        [](Context& c) {
            return drawing::spiral({ c.number("x", 0.5f), c.number("y", 0.5f) }, c.number("innerRadius", 0.0f), c.number("outerRadius", 0.4f),
                                   c.number("turns", 4.0f));
        }));

    // Modifiers: a Drawing in, a new Drawing out.
    auto modifierNode = [](std::string type, std::string name, std::string description, std::vector<ns::PinSignature> settings,
                           std::function<drawing::Drawing(Context&, const drawing::Drawing&)> change) {
        std::vector<ns::PinSignature> inputs { drawingIn("drawing") };
        inputs.insert(inputs.end(), settings.begin(), settings.end());
        return define(std::move(type), std::move(name), "Draw", std::move(description), std::move(inputs), { drawingOut("drawing") },
            [change](Context& c, auto&, juce::String& error) {
                auto wired = c.drawings.find("drawing");
                if (wired == c.drawings.end() || wired->second == nullptr)
                {
                    error = "Needs a drawing wired in.";
                    return false;
                }
                c.drawingOutputs["drawing"] = std::make_shared<drawing::Drawing>(change(c, *wired->second));
                return true;
            });
    };
    definitions.push_back(define("draw.merge", "Merge", "Draw", "Puts up to four drawings together into one.",
        { drawingIn("a"), drawingIn("b"), drawingIn("c"), drawingIn("d") }, { drawingOut("drawing") },
        [](Context& c, auto&, juce::String&) {
            auto result = std::make_shared<drawing::Drawing>();
            for (const char* pin : { "a", "b", "c", "d" })
            {
                auto wired = c.drawings.find(pin);
                if (wired != c.drawings.end() && wired->second != nullptr)
                    result->paths.insert(result->paths.end(), wired->second->paths.begin(), wired->second->paths.end());
            }
            c.drawingOutputs["drawing"] = result;
            return true;
        }));
    definitions.push_back(modifierNode("draw.transform", "Transform",
        "Scales and turns the drawing about the pivot (rotation in degrees, clockwise), then moves it.",
        { floatIn("moveX", 0.0f), floatIn("moveY", 0.0f), floatIn("rotation", 0.0f), floatIn("scaleX", 1.0f), floatIn("scaleY", 1.0f),
          floatIn("pivotX", 0.5f), floatIn("pivotY", 0.5f) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::transformed(d, c.number("moveX", 0.0f), c.number("moveY", 0.0f), c.number("rotation", 0.0f), c.number("scaleX", 1.0f),
                                        c.number("scaleY", 1.0f), { c.number("pivotX", 0.5f), c.number("pivotY", 0.5f) });
        }));
    definitions.push_back(modifierNode("draw.repeat.linear", "Repeat in a Line",
        "Copies in a row: copy i is moved i x (dx, dy), turned i x rotateStep degrees and scaled scaleStep^i about its centre.",
        { intIn("count", 5), floatIn("dx", 0.1f), floatIn("dy", 0.0f), floatIn("rotateStep", 0.0f), floatIn("scaleStep", 1.0f) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::repeatLinear(d, juce::jlimit(0, 10000, c.integer("count", 5)), c.number("dx", 0.1f), c.number("dy", 0.0f),
                                         c.number("rotateStep", 0.0f), c.number("scaleStep", 1.0f));
        }));
    definitions.push_back(modifierNode("draw.repeat.radial", "Repeat Around",
        "Copies turned around a centre, evenly through the sweep (degrees; 360 is all the way round).",
        { intIn("count", 8), floatIn("centerX", 0.5f), floatIn("centerY", 0.5f), floatIn("sweep", 360.0f) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::repeatRadial(d, juce::jlimit(0, 10000, c.integer("count", 8)), { c.number("centerX", 0.5f), c.number("centerY", 0.5f) },
                                         c.number("sweep", 360.0f));
        }));
    definitions.push_back(modifierNode("draw.repeat.grid", "Repeat in a Grid", "Copies in columns and rows, (dx, dy) apart.",
        { intIn("columns", 4), intIn("rows", 4), floatIn("dx", 0.25f), floatIn("dy", 0.25f) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::repeatGrid(d, juce::jlimit(0, 1000, c.integer("columns", 4)), juce::jlimit(0, 1000, c.integer("rows", 4)),
                                       c.number("dx", 0.25f), c.number("dy", 0.25f));
        }));
    definitions.push_back(modifierNode("draw.scatter", "Scatter",
        "Copies placed at random in an area (x, y, width, height), each turned up to +- maxRotation degrees and scaled "
        "between scaleMin and scaleMax. The seed picks the arrangement.",
        { intIn("count", 50), floatIn("x", 0.0f), floatIn("y", 0.0f), floatIn("width", 1.0f), floatIn("height", 1.0f),
          floatIn("maxRotation", 180.0f), floatIn("scaleMin", 0.5f), floatIn("scaleMax", 1.0f), intIn("seed", 1) },
        [](Context& c, const drawing::Drawing& d) {
            const float x = c.number("x", 0.0f), y = c.number("y", 0.0f);
            return drawing::scattered(d, juce::jlimit(0, 100000, c.integer("count", 50)),
                                      { x, y, x + c.number("width", 1.0f), y + c.number("height", 1.0f) }, c.number("maxRotation", 180.0f),
                                      c.number("scaleMin", 0.5f), c.number("scaleMax", 1.0f), c.integer("seed", 1));
        }));
    definitions.push_back(modifierNode("draw.jitter", "Jitter", "Moves every point at random by up to the amount.",
        { floatIn("amount", 0.01f), intIn("seed", 1) },
        [](Context& c, const drawing::Drawing& d) { return drawing::jittered(d, c.number("amount", 0.01f), c.integer("seed", 1)); }));
    definitions.push_back(modifierNode("draw.wobble", "Wobble",
        "Makes lines look hand-drawn: pushes them sideways by up to the amount, with smooth noise that changes over the wavelength.",
        { floatIn("amount", 0.01f), floatIn("wavelength", 0.1f), intIn("seed", 1) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::wobbled(d, c.number("amount", 0.01f), c.number("wavelength", 0.1f), c.integer("seed", 1));
        }));
    definitions.push_back(modifierNode("draw.mirror", "Mirror",
        "A mirror image: Horizontal flips left to right about x = position, Vertical flips top to bottom about y = position. "
        "Keep Original keeps the drawing as well.",
        { enumIn("axis", "Axis", 0), floatIn("position", 0.5f), boolIn("keepOriginal", true) },
        [](Context& c, const drawing::Drawing& d) {
            return drawing::mirrored(d, c.integer("axis", 0) == 0, c.number("position", 0.5f), c.flag("keepOriginal", true));
        }));

    {
        auto script = define("draw.script", "Draw Script", "Draw",
            "A drawing written as commands - the way the AI draws. A pen moves over the canvas ((0, 0) top-left, (1, 1) "
            "bottom-right, heading 0 = right, clockwise): move, line, forward, turn, arc, curve, close; shapes circle, "
            "ellipse, rect, polygon, star; repeat n { }, if / else, let, push / pop, scale, seed. The graph's Variables "
            "can be used by id. See docs/DRAW_SCRIPT.md.",
            { textIn("script"), intIn("seed", 1) }, { drawingOut("drawing") },
            [](Context& c, auto&, juce::String& error) {
                const auto result = draw_script::run(c.text("script").toStdString(), c.numericVariables(), c.integer("seed", 1));
                if (! result.error.empty())
                {
                    error = juce::String(result.error);
                    return false;
                }
                c.drawingOutputs["drawing"] = std::make_shared<drawing::Drawing>(result.drawing);
                return true;
            });
        script.readsVariables = true;
        definitions.push_back(std::move(script));
    }

    // From images: the image decides the drawing.
    definitions.push_back(define("draw.contour", "Contour", "Draw",
        "Traces the lines where the image's brightness crosses the level - outlines of shapes, edges of a pattern, "
        "contour lines of a height map. Lines shorter than minLength (a fraction of the canvas) are left out.",
        { imageIn("image"), floatIn("level", 0.5f), floatIn("minLength", 0.01f) }, { drawingOut("drawing") },
        [needInput](Context& c, auto&, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const auto level = c.number("level", 0.5f);
            // Count first, then trace into a buffer of exactly that size.
            const auto total = c.routines.contour(input->rgba.data(), input->width, input->height, level, nullptr, 0);
            std::vector<float> segments(static_cast<size_t>(juce::jmax<std::int64_t>(0, total)) * 4);
            if (total > 0)
                c.routines.contour(input->rgba.data(), input->width, input->height, level, segments.data(), total);
            c.drawingOutputs["drawing"] = std::make_shared<drawing::Drawing>(
                drawing::joinSegments(segments, input->width, input->height, juce::jmax(0.0f, c.number("minLength", 0.01f))));
            return true;
        }));
    definitions.push_back(define("draw.sample", "Sample", "Draw",
        "Random points where the image is bright (or dark, inverted): the brighter, the more likely. With a drawing wired "
        "in, a copy of it is placed on each point, turned up to +- maxRotation degrees and scaled between scaleMin and "
        "scaleMax; without one, each point is a dot for Paint to stamp.",
        { imageIn("image"), drawingIn("drawing"), intIn("count", 200), intIn("seed", 1), boolIn("invert", false),
          floatIn("maxRotation", 180.0f), floatIn("scaleMin", 0.5f), floatIn("scaleMax", 1.0f) },
        { drawingOut("drawing") },
        [needInput](Context& c, auto&, juce::String& error) {
            auto input = needInput(c, "image", error);
            if (input == nullptr) return false;
            const auto count = juce::jlimit(0, 1000000, c.integer("count", 200));
            std::vector<float> xy(static_cast<size_t>(count) * 2);
            const auto placed = count == 0 ? 0 : c.routines.sample(input->rgba.data(), input->width, input->height, count, c.integer("seed", 1),
                                                                    c.flag("invert", false) ? 1 : 0, xy.data(), static_cast<std::int64_t>(count) * 50);
            std::vector<drawing::Point> points;
            for (std::int64_t i = 0; i < placed; ++i)
                points.push_back({ xy[static_cast<size_t>(i) * 2], xy[static_cast<size_t>(i) * 2 + 1] });
            auto result = std::make_shared<drawing::Drawing>();
            auto wired = c.drawings.find("drawing");
            if (wired != c.drawings.end() && wired->second != nullptr)
                *result = drawing::placedAt(*wired->second, points, c.number("maxRotation", 180.0f), c.number("scaleMin", 0.5f),
                                            c.number("scaleMax", 1.0f), c.integer("seed", 1));
            else
                for (const auto& p : points)
                    result->paths.push_back({ { p }, false });
            c.drawingOutputs["drawing"] = result;
            return true;
        }));

    definitions.push_back(define("draw.brush", "Brush", "Draw",
        "How a drawing is painted. Size is the stamp's width as a fraction of the canvas's shorter side; spacing is the gap "
        "between stamps as a fraction of the size; hardness 1 is a crisp edge, 0 soft from the centre. Angle is in degrees. "
        "Scatter moves each stamp at random (a fraction of the size); size and opacity jitter shrink or fade stamps at "
        "random; taper start / end grow and shrink the stroke over that fraction of its length; seed picks the randomness.",
        { enumIn("tip", "BrushTip", 0), imageIn("tipImage"), floatIn("size", 0.02f), floatIn("hardness", 0.8f), floatIn("spacing", 0.1f),
          colourIn("color", 1.0f, 1.0f, 1.0f), floatIn("opacity", 1.0f), enumIn("rotation", "BrushRotation", 0), floatIn("angle", 0.0f),
          floatIn("scatter", 0.0f), floatIn("sizeJitter", 0.0f), floatIn("opacityJitter", 0.0f), floatIn("taperStart", 0.0f),
          floatIn("taperEnd", 0.0f), intIn("seed", 1) },
        { brushOut("brush") },
        [](Context& c, auto&, juce::String& error) {
            auto brush = std::make_shared<drawing::Brush>();
            brush->tip = static_cast<drawing::Brush::Tip>(juce::jlimit(0, 4, c.integer("tip", 0)));
            if (brush->tip == drawing::Brush::Tip::image)
            {
                auto wired = c.inputs.find("tipImage");
                if (wired == c.inputs.end() || wired->second == nullptr)
                {
                    error = "An Image tip needs an image wired into tipImage.";
                    return false;
                }
                auto tip = std::make_shared<drawing::TipImage>();
                tip->width = wired->second->width;
                tip->height = wired->second->height;
                tip->rgba = wired->second->rgba;
                brush->tipImage = tip;
            }
            brush->size = juce::jmax(0.0f, c.number("size", 0.02f));
            brush->hardness = juce::jlimit(0.0f, 1.0f, c.number("hardness", 0.8f));
            brush->spacing = juce::jlimit(0.01f, 10.0f, c.number("spacing", 0.1f));
            const auto colour = c.colour("color", { 1.0f, 1.0f, 1.0f });
            brush->red = colour.x;
            brush->green = colour.y;
            brush->blue = colour.z;
            brush->opacity = juce::jlimit(0.0f, 1.0f, c.number("opacity", 1.0f));
            brush->rotation = static_cast<drawing::Brush::Rotation>(juce::jlimit(0, 2, c.integer("rotation", 0)));
            brush->angle = c.number("angle", 0.0f);
            brush->scatter = juce::jmax(0.0f, c.number("scatter", 0.0f));
            brush->sizeJitter = juce::jlimit(0.0f, 1.0f, c.number("sizeJitter", 0.0f));
            brush->opacityJitter = juce::jlimit(0.0f, 1.0f, c.number("opacityJitter", 0.0f));
            brush->taperStart = juce::jlimit(0.0f, 1.0f, c.number("taperStart", 0.0f));
            brush->taperEnd = juce::jlimit(0.0f, 1.0f, c.number("taperEnd", 0.0f));
            brush->seed = c.integer("seed", 1);
            c.brushOutputs["brush"] = brush;
            return true;
        }));

    definitions.push_back(define("draw.paint", "Paint", "Draw",
        "Paints a drawing onto the canvas image (or onto a new transparent image of width x height): Stroke runs the "
        "brush along its lines, Fill fills its shapes with the fill colour. The result goes on down the graph like any image.",
        { imageIn("canvas"), intIn("width", 1024), intIn("height", 1024), drawingIn("drawing"), brushIn("brush"),
          enumIn("mode", "PaintMode", 0), colourIn("fill", 1.0f, 1.0f, 1.0f) },
        { imageOut("image") },
        [](Context& c, auto& out, juce::String& error) {
            auto wired = c.drawings.find("drawing");
            if (wired == c.drawings.end() || wired->second == nullptr)
            {
                error = "Paint needs a drawing wired in.";
                return false;
            }
            const auto& shapes = *wired->second;
            auto brushIt = c.brushes.find("brush");
            const drawing::Brush brush = brushIt != c.brushes.end() && brushIt->second != nullptr ? *brushIt->second : drawing::Brush {};

            auto canvasIt = c.inputs.find("canvas");
            auto image = canvasIt != c.inputs.end() && canvasIt->second != nullptr ? copyOf(canvasIt->second)
                                                                                     : blank(c.integer("width", 1024), c.integer("height", 1024), false);
            image->data = false;
            const int w = image->width, h = image->height;
            const float sx = static_cast<float>(w), sy = static_cast<float>(h);
            const auto count = pixelCount(*image);
            std::vector<float> mask(static_cast<size_t>(count), 0.0f);
            const int mode = juce::jlimit(0, 2, c.integer("mode", 0));

            if (mode >= 1) // fill: every path's edges together, each path closed
            {
                std::vector<float> edges;
                for (const auto& path : shapes.paths)
                {
                    const auto n = path.points.size();
                    if (n < 3)
                        continue;
                    for (size_t i = 0; i < n; ++i)
                    {
                        const auto& a = path.points[i];
                        const auto& b = path.points[(i + 1) % n];
                        edges.insert(edges.end(), { a.x * sx, a.y * sy, b.x * sx, b.y * sy });
                    }
                }
                const auto m = static_cast<std::int64_t>(edges.size() / 4);
                if (m > 0)
                {
                    std::vector<float> xs(static_cast<size_t>(m)), dirs(static_cast<size_t>(m));
                    if (! ok(c.routines.fillShapes(mask.data(), w, h, edges.data(), m, xs.data(), dirs.data()), "Fill", error))
                        return false;
                    const auto fill = c.colour("fill", { 1.0f, 1.0f, 1.0f });
                    if (! ok(c.routines.composite(image->rgba.data(), mask.data(), count, fill.x, fill.y, fill.z, brush.opacity), "Fill", error))
                        return false;
                }
            }
            if (mode != 1) // stroke
            {
                std::fill(mask.begin(), mask.end(), 0.0f);
                // Where the stamps go is geometry (Drawing.cpp); laying them down is FRust (dr_stamps).
                std::vector<float> stamps;
                for (size_t p = 0; p < shapes.paths.size(); ++p)
                    for (const auto& stamp : drawing::placeStamps(shapes.paths[p], brush, w, h, static_cast<int>(p)))
                        stamps.insert(stamps.end(), { stamp.x, stamp.y, stamp.radius, stamp.angle, stamp.opacity });
                const float noTip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                const auto* tip = brush.tipImage.get();
                const bool useTip = tip != nullptr && tip->width > 0 && tip->height > 0;
                if (! stamps.empty()
                    && ! ok(c.routines.stamps(mask.data(), w, h, stamps.data(), static_cast<std::int64_t>(stamps.size() / 5),
                                              static_cast<std::int64_t>(brush.tip), brush.hardness, useTip ? tip->rgba.data() : noTip,
                                              useTip ? tip->width : 1, useTip ? tip->height : 1, brush.seed), "Stroke", error))
                    return false;
                if (! ok(c.routines.composite(image->rgba.data(), mask.data(), count, brush.red, brush.green, brush.blue, brush.opacity),
                         "Stroke", error))
                    return false;
            }
            out["image"] = image;
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
    registry.RegisterDiagramType({ kImageDiagram, "Image", "Makes an image: generators, effects, drawing and surface maps." });
    for (const auto& e : enums)
        registry.RegisterEnum(e);
    for (const auto& d : definitions)
        registry.Register(d.descriptor);
    ns::RegisterSymbolGetNodes(registry); // params / constants / variables (shared/NodeSystem/SYMBOLS.md)
    ns::RegisterGraphNode(registry, { kImageDiagram }); // another image graph used as a node (GRAPH_TYPES.md)
    // Decisions (FLOW.md): Switch and Route for every kind of value an image graph carries.
    std::vector<ns::FlowType> flowTypes;
    for (auto type : { ns::DataType::Texture, ns::DataType::Float, ns::DataType::Int, ns::DataType::Bool, ns::DataType::Color,
                       ns::DataType::String, ns::DataType::Drawing, ns::DataType::Brush })
        flowTypes.push_back(ns::StandardFlowType(type));
    ns::RegisterFlowNodes(registry, flowTypes, { kImageDiagram });
    // Struct nodes (TYPES.md): making, taking apart and changing structs, for building algorithms.
    ns::RegisterStructNodes(registry, { kImageDiagram });
    ns::RegisterEnumNodes(registry, { kImageDiagram }); // enums whose values carry data: Make Variant, Match
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
    : library(lib), host(std::move(h)), routines(std::make_shared<Routines>()), surfaceMaps(std::make_shared<surface_maps::Engine>())
{
}

Evaluator::Evaluator(const Library& lib, Host h, std::shared_ptr<Routines> sharedRoutines, std::shared_ptr<surface_maps::Engine> sharedMaps)
    : library(lib), host(std::move(h)), routines(std::move(sharedRoutines)), surfaceMaps(std::move(sharedMaps))
{
}

Host::LoadedGraph readGraphDocument(const juce::String& json)
{
    Host::LoadedGraph result;
    const auto doc = juce::JSON::parse(json);
    if (doc["format"].toString() != kGraphDocumentFormat)
    {
        result.error = "not an image graph";
        return result;
    }
    result.text = doc["graph"].toString().toStdString();
    std::string parseError;
    auto graph = ns::DeserializeGraph(result.text, parseError);
    if (graph == nullptr)
    {
        result.error = juce::String(parseError);
        return result;
    }
    graph->SetTarget(ns::GraphTarget::Dataflow);
    graph->SetDiagramType(kImageDiagram);
    result.graph = std::shared_ptr<const ns::Graph>(std::move(graph));
    return result;
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

bool Evaluator::readSelector(const ns::Graph& graph, const ns::Node& node, ns::PinDefaultValue& selector, std::string& signature,
                             juce::String& error, int depth)
{
    for (const auto& pin : node.Inputs())
    {
        if (pin.name != ns::kFlowSelectorPin)
            continue;
        selector = pin.defaultValue;
        for (const auto& wire : graph.Connections())
            if (wire.toNode == node.Id() && wire.toPin == pin.id)
            {
                const auto* from = graph.FindNode(wire.fromNode);
                const auto* fromPin = from != nullptr ? from->FindPin(wire.fromPin) : nullptr;
                if (fromPin == nullptr)
                    break;
                std::string upstream;
                if (! evaluateNode(graph, wire.fromNode, { fromPin->name }, upstream, error, depth + 1))
                    return false;
                auto value = cache[wire.fromNode].values.find(fromPin->name);
                if (value == cache[wire.fromNode].values.end())
                {
                    error = "The selector needs a value (a number, integer, toggle or choice).";
                    return false;
                }
                selector = value->second;
                signature += "|selector<" + upstream;
                return true;
            }
        signature += "|selector=" + valueText(selector);
        return true;
    }
    return true;
}

std::string Evaluator::flowChosenCase(const ns::Graph& graph, ns::NodeId id, juce::String& error)
{
    const auto* node = graph.FindNode(id);
    if (node == nullptr)
        return {};
    ns::PinDefaultValue selector;
    std::string signature;
    if (! readSelector(graph, *node, selector, signature, error, 0))
        return {};
    const auto cases = ns::FlowCasePins(*node);
    return cases.empty() ? std::string() : cases[static_cast<size_t>(ns::FlowCaseIndex(selector, static_cast<int>(cases.size())))]->name;
}

bool Evaluator::evaluateFlowNode(const ns::Graph& graph, const ns::Node& node, ns::FlowKind kind, const std::vector<std::string>& wanted,
                                 std::string& signatureOut, juce::String& error, int depth)
{
    const auto title = juce::String(kind == ns::FlowKind::route ? "Route" : "Switch");
    std::string signature = node.TypeName();
    ns::PinDefaultValue selector;
    if (! readSelector(graph, node, selector, signature, error, depth))
        return false;
    const auto cases = ns::FlowCasePins(node);
    if (cases.empty())
    {
        error = title + ": it has no cases.";
        return false;
    }
    const auto* chosen = cases[static_cast<size_t>(ns::FlowCaseIndex(selector, static_cast<int>(cases.size())))];

    // A Route's other outputs carry nothing: whatever asks for one is not on the chosen path.
    std::string outputName = ns::kFlowValuePin;
    if (kind == ns::FlowKind::route)
    {
        for (const auto& w : wanted)
            if (w != chosen->name)
            {
                error = "Not chosen - the Route sends to " + juce::String(chosen->name) + ".";
                return false;
            }
        outputName = chosen->name;
    }

    // The one input that flows: the chosen case (Switch) or the Route's input. Nothing else is computed.
    const ns::Pin* source = chosen;
    if (kind == ns::FlowKind::route)
        for (const auto& pin : node.Inputs())
            if (pin.name == ns::kFlowValuePin)
                source = &pin;

    Cached result;
    const ns::Connection* wire = nullptr;
    for (const auto& connection : graph.Connections())
        if (connection.toNode == node.Id() && connection.toPin == source->id)
            wire = &connection;
    if (wire != nullptr)
    {
        const auto* from = graph.FindNode(wire->fromNode);
        const auto* fromPin = from != nullptr ? from->FindPin(wire->fromPin) : nullptr;
        if (fromPin == nullptr)
        {
            error = title + ": a wire leads nowhere.";
            return false;
        }
        std::string upstream;
        if (! evaluateNode(graph, wire->fromNode, { fromPin->name }, upstream, error, depth + 1))
            return false;
        signature += "|" + source->name + "<" + upstream;
        auto& up = cache[wire->fromNode];
        switch (fromPin->type.dataType)
        {
            case ns::DataType::Texture: result.outputs[outputName] = up.outputs[fromPin->name]; break;
            case ns::DataType::Drawing: result.drawings[outputName] = up.drawings[fromPin->name]; break;
            case ns::DataType::Brush: result.brushes[outputName] = up.brushes[fromPin->name]; break;
            case ns::DataType::Struct: result.structs[outputName] = up.structs[fromPin->name]; break;
            default:
                result.values[outputName] = up.values[fromPin->name];
                if (auto full = up.structs.find(fromPin->name); full != up.structs.end())
                    result.structs[outputName] = full->second; // an enum carrying data passes through whole
                break;
        }
    }
    else
    {
        const auto type = source->type.dataType;
        if (type == ns::DataType::Texture || type == ns::DataType::Drawing || type == ns::DataType::Brush || type == ns::DataType::Struct)
        {
            error = title + ": nothing is wired into " + juce::String(source->name) + ".";
            return false;
        }
        signature += "|" + source->name + "=" + valueText(source->defaultValue);
        result.values[outputName] = source->defaultValue;
    }
    signatureOut = std::to_string(std::hash<std::string> {}(signature));
    result.signature = signatureOut;
    cache[node.Id()] = std::move(result);
    return true;
}

bool Evaluator::evaluateGraphNode(const ns::Node& node, const Host::LoadedGraph& usedGraph, Context& context,
                                  std::map<std::string, ImagePtr>& outputs, juce::String& error)
{
    if (host.depth >= 16)
    {
        error = "graphs use each other too deeply - does a graph use itself?";
        return false;
    }
    const auto path = ns::GraphNodePath(node);
    auto& entry = used[path];
    if (entry.evaluator == nullptr || entry.text != usedGraph.text)
    {
        Host child;
        child.loadImage = host.loadImage;
        child.loadGraph = host.loadGraph;
        child.depth = host.depth + 1;
        child.structs = host.structs;
        child.enums = host.enums;
        entry.evaluator.reset(new Evaluator(library, std::move(child), routines, surfaceMaps));
        entry.text = usedGraph.text;
    }
    auto& childHost = entry.evaluator->host;
    childHost.paramOverrides.clear();
    childHost.graphInputs.clear();
    childHost.graphInputKeys.clear();
    for (const auto& pin : node.Inputs())
    {
        if (pin.name == ns::kGraphPathPin)
            continue;
        if (pin.type.dataType == ns::DataType::Texture)
        {
            auto image = context.inputs.find(pin.name);
            if (image != context.inputs.end() && image->second != nullptr)
            {
                childHost.graphInputs[pin.name] = image->second;
                // A computed image is never changed in place, so its address names it.
                childHost.graphInputKeys[pin.name] = std::to_string(reinterpret_cast<std::uintptr_t>(image->second.get()));
            }
        }
        else if (const auto* symbol = usedGraph.graph->FindSymbol(pin.name); symbol != nullptr && symbol->kind == ns::SymbolKind::Param)
        {
            if (const auto* value = context.setting(pin.name); value != nullptr && ! std::holds_alternative<std::monostate>(*value))
                childHost.paramOverrides[pin.name] = *value;
        }
    }

    for (const auto& wanted : context.wantedOutputs)
    {
        const ns::Node* outputNode = nullptr;
        for (const auto& [id, candidate] : usedGraph.graph->Nodes())
        {
            const auto* descriptor = library.find(candidate->TypeName());
            if (descriptor != nullptr && descriptor->descriptor.graphPort == ns::GraphPort::output
                && ns::GraphPortName(*candidate, ns::GraphPort::output) == wanted)
                outputNode = candidate.get();
        }
        if (outputNode == nullptr || outputNode->Outputs().empty())
        {
            error = "the graph has no output called " + juce::String(wanted) + " any more.";
            return false;
        }
        juce::String inner;
        auto image = entry.evaluator->evaluate(*usedGraph.graph, outputNode->Id(), outputNode->Outputs().front().name, inner);
        if (image == nullptr)
        {
            error = juce::String(path) + ": " + inner;
            return false;
        }
        outputs[wanted] = image;
    }
    return true;
}

drawing::DrawingPtr Evaluator::evaluateDrawing(const ns::Graph& graph, ns::NodeId node, const std::string& output, juce::String& error)
{
    if (! isReady())
    {
        error = getError();
        return nullptr;
    }
    std::string signature;
    if (! evaluateNode(graph, node, { output }, signature, error, 0))
        return nullptr;
    auto found = cache[node].drawings.find(output);
    if (found == cache[node].drawings.end())
    {
        error = "The node has no drawing output called " + juce::String(output) + ".";
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
        if (symbol->type == ns::DataType::Struct)
        {
            // A struct param: the whole struct, to send along or take apart (TYPES.md).
            const auto* def = findStruct(graph, symbol->structType);
            if (def == nullptr)
            {
                error = "The struct " + juce::String(symbol->structType) + " of " + juce::String(symbol->name) + " is not in scope any more.";
                return false;
            }
            // The struct itself too: a member added or its default changed is a different value.
            std::string text = node->TypeName() + "|" + symbol->id + ":" + def->name;
            for (const auto& m : def->members)
                text += ";" + m.name + "/" + std::to_string(static_cast<int>(m.type.dataType)) + m.type.structType + "=" + valueText(m.defaultValue);
            for (const auto& v : symbol->memberValues)
                text += "," + valueText(v);
            signatureOut = std::to_string(std::hash<std::string> {}(text));
            auto& cachedGet = cache[id];
            if (cachedGet.signature != signatureOut || cachedGet.structs.count(ns::kSymbolValuePin) == 0)
            {
                cachedGet = {};
                cachedGet.signature = signatureOut;
                cachedGet.structs[ns::kSymbolValuePin] = structWithDefaults(graph, *def, &symbol->memberValues, 0);
            }
            return true;
        }
        auto value = symbol->value;
        if (symbol->kind == ns::SymbolKind::Param)
        {
            auto overridden = host.paramOverrides.find(symbol->id);
            if (overridden != host.paramOverrides.end())
                value = overridden->second;
        }
        // A Choice of an enum whose values carry data: what the chosen value carries, and the enum itself, count too.
        const auto* carrying = findEnum(graph, symbol->enumType);
        if (carrying != nullptr && ! ns::EnumCarriesValues(*carrying))
            carrying = nullptr;
        std::string text = node->TypeName() + "|" + symbol->id + "=" + valueText(value);
        if (carrying != nullptr)
        {
            for (const auto& v : symbol->memberValues)
                text += "," + valueText(v);
            text += "|" + ns::FrustEnumDeclaration(*carrying);
        }
        signatureOut = std::to_string(std::hash<std::string> {}(text));
        auto& cachedGet = cache[id];
        cachedGet.signature = signatureOut;
        cachedGet.outputs.clear();
        cachedGet.drawings.clear();
        cachedGet.brushes.clear();
        cachedGet.structs.clear();
        cachedGet.values = { { ns::kSymbolValuePin, value } };
        if (carrying != nullptr)
        {
            const auto* index = std::get_if<std::int64_t>(&value);
            // What it carries belongs to the param's own value; an outside value (an Automation) picks another variant,
            // which then carries its defaults.
            const bool own = index != nullptr && symbol->value == value;
            cachedGet.structs[ns::kSymbolValuePin] = enumWithDefaults(graph, *carrying, index != nullptr ? static_cast<int>(*index) : 0,
                                                                      own ? &symbol->memberValues : nullptr, 0);
        }
        return true;
    }

    if (const auto kind = ns::FlowKindOf(*node); kind != ns::FlowKind::none)
        return evaluateFlowNode(graph, *node, kind, wanted, signatureOut, error, depth);

    const bool graphNode = ns::IsGraphNode(*node);
    const auto structKind = ns::StructNodeKindOf(*node);
    const bool structNode = structKind != ns::StructNodeKind::none;
    const auto enumKind = ns::EnumNodeKindOf(*node);
    const bool enumNode = enumKind != ns::EnumNodeKind::none;
    const auto* definition = graphNode || structNode || enumNode ? nullptr : library.find(node->TypeName());
    if (! graphNode && ! structNode && ! enumNode && definition == nullptr)
    {
        error = "Unknown node type " + juce::String(node->TypeName()) + ".";
        return false;
    }

    // Wired image inputs first (each computes only the output it is asked for), building this node's signature.
    Context context { *node, host, *routines, *surfaceMaps, {}, {}, {}, {} };
    context.graph = &graph;
    std::string signature = node->TypeName();
    if (definition != nullptr && definition->readsVariables)
        for (const auto& [name, value] : context.numericVariables())
            signature += "|$" + name + "=" + std::to_string(value);
    if (definition != nullptr && definition->readsGraphInputs)
        for (const auto& [name, key] : host.graphInputKeys)
            signature += "|@" + name + "=" + key;
    // A Graph node depends on the graph it uses, too.
    Host::LoadedGraph usedGraph;
    if (graphNode)
    {
        const auto path = ns::GraphNodePath(*node);
        if (path.empty())
        {
            error = "A Graph node needs a graph - choose one in its Properties.";
            return false;
        }
        if (! host.loadGraph)
        {
            error = "Graph nodes cannot be used here.";
            return false;
        }
        usedGraph = host.loadGraph(juce::String(path));
        if (usedGraph.graph == nullptr)
        {
            error = "The graph " + juce::String(path) + " cannot be read" + (usedGraph.error.isNotEmpty() ? ": " + usedGraph.error : juce::String(".")) ;
            return false;
        }
        signature += "|graph=" + std::to_string(std::hash<std::string> {}(usedGraph.text));
    }
    for (const auto& pin : node->Inputs())
    {
        const ns::Connection* wire = nullptr;
        for (const auto& connection : graph.Connections())
            if (connection.toNode == id && connection.toPin == pin.id)
                wire = &connection;

        if (wire == nullptr)
        {
            signature += "|" + pin.name + "=" + valueText(pin.defaultValue);
            // A Graph node's image input may name a project image instead of being wired.
            if (graphNode && pin.type.dataType == ns::DataType::Texture && host.loadImage)
                if (const auto* path = std::get_if<std::string>(&pin.defaultValue); path != nullptr && ! path->empty())
                    context.inputs[pin.name] = host.loadImage(juce::String(*path));
            continue;
        }

        const auto* from = graph.FindNode(wire->fromNode);
        const auto* fromPin = from != nullptr ? from->FindPin(wire->fromPin) : nullptr;
        if (fromPin == nullptr)
            continue;
        std::string upstream;
        if (! evaluateNode(graph, wire->fromNode, { fromPin->name }, upstream, error, depth + 1))
            return false;
        auto& upstreamResult = cache[wire->fromNode];
        switch (fromPin->type.dataType)
        {
            case ns::DataType::Texture: context.inputs[pin.name] = upstreamResult.outputs[fromPin->name]; break;
            case ns::DataType::Drawing: context.drawings[pin.name] = upstreamResult.drawings[fromPin->name]; break;
            case ns::DataType::Brush: context.brushes[pin.name] = upstreamResult.brushes[fromPin->name]; break;
            case ns::DataType::Struct: context.structs[pin.name] = upstreamResult.structs[fromPin->name]; break;
            default:
                context.wiredValues[pin.name] = upstreamResult.values[fromPin->name];
                // An enum value carrying data: its full value comes along with its number.
                if (auto full = upstreamResult.structs.find(fromPin->name); full != upstreamResult.structs.end())
                    context.structs[pin.name] = full->second;
                break;
        }
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
            complete = complete && (cached.outputs.count(name) > 0 || cached.values.count(name) > 0
                                    || cached.drawings.count(name) > 0 || cached.brushes.count(name) > 0
                                    || cached.structs.count(name) > 0);
        if (complete)
            return true;
        for (const auto& [name, image] : cached.outputs)
            needed.insert(name); // recompute together with what was already there
    }

    context.wantedOutputs.assign(needed.begin(), needed.end());
    std::map<std::string, ImagePtr> outputs;
    juce::String nodeError;
    const bool evaluated = graphNode    ? evaluateGraphNode(*node, usedGraph, context, outputs, nodeError)
                           : structNode ? evaluateStructNode(graph, structKind, context, outputs, nodeError)
                           : enumNode   ? evaluateEnumNode(graph, enumKind, context, outputs, nodeError)
                                        : definition->evaluate(context, outputs, nodeError);
    if (! evaluated)
    {
        const auto title = graphNode ? juce::String("Graph")
                         : structNode ? juce::String(structKind == ns::StructNodeKind::make         ? "Make Struct"
                                                     : structKind == ns::StructNodeKind::breakApart ? "Break Struct"
                                                     : structKind == ns::StructNodeKind::setMembers ? "Set Members"
                                                                                                    : "Get Member")
                         : enumNode   ? juce::String(enumKind == ns::EnumNodeKind::makeVariant ? "Make Variant" : "Match")
                                      : juce::String(definition->descriptor.displayName);
        error = title + ": " + nodeError;
        cached = {};
        return false;
    }
    cached.signature = signatureOut;
    cached.outputs = std::move(outputs);
    cached.values = std::move(context.valueOutputs);
    cached.drawings = std::move(context.drawingOutputs);
    cached.brushes = std::move(context.brushOutputs);
    cached.structs = std::move(context.structOutputs);
    return true;
}

const ns::StructDef* Evaluator::findStruct(const ns::Graph& graph, const std::string& name) const
{
    if (const auto* own = graph.FindStruct(name))
        return own;
    for (const auto& def : host.structs)
        if (def.name == name)
            return &def;
    return nullptr;
}

const ns::EnumDef* Evaluator::findEnum(const ns::Graph& graph, const std::string& name) const
{
    if (name.empty())
        return nullptr;
    if (const auto* own = graph.FindEnum(name))
        return own;
    for (const auto& def : host.enums)
        if (def.name == name)
            return &def;
    for (const auto& def : library.getEnums())
        if (def.name == name)
            return &def;
    return nullptr;
}

bool Evaluator::carriesValues(const ns::Graph& graph, const ns::PinTypeDesc& type) const
{
    const auto* def = type.enumType.empty() ? nullptr : findEnum(graph, type.enumType);
    return def != nullptr && ns::EnumCarriesValues(*def);
}

void Evaluator::putDefault(const ns::Graph& graph, StructValue& into, const std::string& key, const ns::PinTypeDesc& type,
                           const ns::PinDefaultValue& value, int depth) const
{
    switch (type.dataType)
    {
        case ns::DataType::Texture:
            // An image member may name a project image.
            if (const auto* path = std::get_if<std::string>(&value); path != nullptr && ! path->empty() && host.loadImage)
                if (auto image = host.loadImage(juce::String(*path)))
                    into.images[key] = image;
            break;
        case ns::DataType::Struct:
            if (const auto* inner = findStruct(graph, type.structType); inner != nullptr && depth < 8)
                into.structs[key] = structWithDefaults(graph, *inner, nullptr, depth + 1);
            break;
        case ns::DataType::Drawing:
        case ns::DataType::Brush:
            break; // nothing until one is wired in
        default:
        {
            into.values[key] = std::holds_alternative<std::monostate>(value) ? ns::DefaultValueFor(type.dataType) : value;
            // An enum whose values carry data: the full value travels alongside its number. Recursive types (a list
            // whose value carries a list) stop after a few levels - the default of a default needs no more.
            if (const auto* def = carriesValues(graph, type) ? findEnum(graph, type.enumType) : nullptr; def != nullptr && depth < 8)
            {
                const auto* index = std::get_if<std::int64_t>(&into.values[key]);
                into.structs[key] = enumWithDefaults(graph, *def, index != nullptr ? static_cast<int>(*index) : 0, nullptr, depth + 1);
            }
            break;
        }
    }
}

StructPtr Evaluator::structWithDefaults(const ns::Graph& graph, const ns::StructDef& def, const std::vector<ns::PinDefaultValue>* memberValues,
                                        int depth) const
{
    auto made = std::make_shared<StructValue>();
    made->type = def.name;
    for (size_t i = 0; i < def.members.size(); ++i)
    {
        const auto& member = def.members[i];
        const auto& value = memberValues != nullptr && i < memberValues->size() && ! std::holds_alternative<std::monostate>((*memberValues)[i])
                                ? (*memberValues)[i]
                                : member.defaultValue;
        putDefault(graph, *made, ns::StructMemberPinName(member.name), member.type, value, depth);
    }
    return made;
}

StructPtr Evaluator::enumWithDefaults(const ns::Graph& graph, const ns::EnumDef& def, int variant, const std::vector<ns::PinDefaultValue>* fieldValues,
                                      int depth) const
{
    auto made = std::make_shared<StructValue>();
    made->type = def.name;
    made->variant = def.variants.empty() ? 0 : juce::jlimit(0, static_cast<int>(def.variants.size()) - 1, variant);
    if (def.variants.empty())
        return made;
    const auto& fields = def.variants[static_cast<size_t>(made->variant)].fields;
    for (size_t i = 0; i < fields.size(); ++i)
    {
        const ns::PinDefaultValue none;
        const auto& value = fieldValues != nullptr && i < fieldValues->size() ? (*fieldValues)[i] : none;
        putDefault(graph, *made, ns::StructMemberPinName(fields[i].name), fields[i].type, value, depth);
    }
    return made;
}

void Evaluator::takeMember(const ns::Graph& graph, Context& context, const ns::Pin& pin, StructValue& into) const
{
    switch (pin.type.dataType)
    {
        case ns::DataType::Texture:
        {
            into.images.erase(pin.name);
            auto image = context.inputs.find(pin.name);
            if (image != context.inputs.end() && image->second != nullptr)
                into.images[pin.name] = image->second;
            else if (const auto* path = std::get_if<std::string>(&pin.defaultValue); path != nullptr && ! path->empty() && host.loadImage)
                if (auto loaded = host.loadImage(juce::String(*path)))
                    into.images[pin.name] = loaded;
            break;
        }
        case ns::DataType::Drawing:
            into.drawings.erase(pin.name);
            if (auto d = context.drawings.find(pin.name); d != context.drawings.end())
                into.drawings[pin.name] = d->second;
            break;
        case ns::DataType::Brush:
            into.brushes.erase(pin.name);
            if (auto b = context.brushes.find(pin.name); b != context.brushes.end())
                into.brushes[pin.name] = b->second;
            break;
        case ns::DataType::Struct:
            if (auto st = context.structs.find(pin.name); st != context.structs.end() && st->second != nullptr)
                into.structs[pin.name] = st->second;
            else if (const auto* inner = findStruct(graph, pin.type.structType))
                into.structs[pin.name] = structWithDefaults(graph, *inner, nullptr, 0);
            break;
        default:
            if (const auto* value = context.setting(pin.name))
                into.values[pin.name] = *value;
            // An enum whose values carry data: the full value wired in, else the typed-in value with what it carries
            // by default.
            into.structs.erase(pin.name);
            if (carriesValues(graph, pin.type))
            {
                if (auto st = context.structs.find(pin.name); st != context.structs.end() && st->second != nullptr)
                    into.structs[pin.name] = st->second;
                else if (const auto* def = findEnum(graph, pin.type.enumType))
                {
                    const auto* index = std::get_if<std::int64_t>(&into.values[pin.name]);
                    into.structs[pin.name] = enumWithDefaults(graph, *def, index != nullptr ? static_cast<int>(*index) : 0, nullptr, 0);
                }
            }
            break;
    }
}

bool Evaluator::giveMember(const StructValue& from, const std::string& key, const ns::Pin& out, Context& context,
                           std::map<std::string, ImagePtr>& outputs, juce::String& error) const
{
    switch (out.type.dataType)
    {
        case ns::DataType::Texture:
        {
            auto image = from.images.find(key);
            if (image == from.images.end())
            {
                if (context.wants(out.name))
                {
                    error = juce::String(out.name) + " has no image - wire one in where the value is made, or choose one in its param.";
                    return false;
                }
                break;
            }
            outputs[out.name] = image->second;
            break;
        }
        case ns::DataType::Drawing:
            if (auto d = from.drawings.find(key); d != from.drawings.end())
                context.drawingOutputs[out.name] = d->second;
            break;
        case ns::DataType::Brush:
            if (auto b = from.brushes.find(key); b != from.brushes.end())
                context.brushOutputs[out.name] = b->second;
            break;
        case ns::DataType::Struct:
            if (auto st = from.structs.find(key); st != from.structs.end())
                context.structOutputs[out.name] = st->second;
            break;
        default:
            if (auto v = from.values.find(key); v != from.values.end())
                context.valueOutputs[out.name] = v->second;
            else
                context.valueOutputs[out.name] = ns::DefaultValueFor(out.type.dataType);
            // An enum carrying data: its full value goes along too.
            if (auto st = from.structs.find(key); st != from.structs.end())
                context.structOutputs[out.name] = st->second;
            break;
    }
    return true;
}

bool Evaluator::evaluateStructNode(const ns::Graph& graph, ns::StructNodeKind kind, Context& context, std::map<std::string, ImagePtr>& outputs,
                                   juce::String& error)
{
    const auto& node = context.node;
    const auto typeName = ns::StructNodeType(node);
    if (typeName.empty())
    {
        error = "choose its struct in Properties.";
        return false;
    }
    const auto* def = findStruct(graph, typeName);
    if (def == nullptr)
    {
        error = "the struct " + juce::String(typeName) + " is not in scope any more.";
        return false;
    }
    auto isFixed = [](const ns::Pin& pin) {
        return pin.name == ns::kStructTypePin || pin.name == ns::kStructValuePin || pin.name == ns::kStructMembersPin
            || pin.name == ns::kStructMemberPin;
    };

    // The struct coming in: wired, or the struct's defaults when nothing is.
    StructPtr incoming;
    if (kind != ns::StructNodeKind::make)
    {
        auto wired = context.structs.find(ns::kStructValuePin);
        incoming = wired != context.structs.end() && wired->second != nullptr ? wired->second : structWithDefaults(graph, *def, nullptr, 0);
    }

    if (kind == ns::StructNodeKind::make || kind == ns::StructNodeKind::setMembers)
    {
        // Members in: each one's wired value, else what is typed into the node.
        auto made = kind == ns::StructNodeKind::make ? std::make_shared<StructValue>() : std::make_shared<StructValue>(*incoming);
        made->type = def->name;
        for (const auto& pin : node.Inputs())
            if (! isFixed(pin))
                takeMember(graph, context, pin, *made);
        context.structOutputs[ns::kStructValuePin] = std::move(made);
        return true;
    }

    // Break Struct / Get Member: members out.
    for (const auto& pin : node.Outputs())
        if (! isFixed(pin) && ! giveMember(*incoming, pin.name, pin, context, outputs, error))
            return false;
    return true;
}

bool Evaluator::evaluateEnumNode(const ns::Graph& graph, ns::EnumNodeKind kind, Context& context, std::map<std::string, ImagePtr>& outputs,
                                 juce::String& error)
{
    const auto& node = context.node;
    const auto typeName = ns::StructNodeText(node, ns::kEnumTypePin);
    if (typeName.empty())
    {
        error = "choose its enum in Properties.";
        return false;
    }
    const auto* def = findEnum(graph, typeName);
    if (def == nullptr || def->variants.empty())
    {
        error = "the enum " + juce::String(typeName) + " is not in scope any more.";
        return false;
    }

    if (kind == ns::EnumNodeKind::makeVariant)
    {
        // The chosen value, with what it carries: each wired in, else typed into the node.
        const auto chosen = ns::StructNodeText(node, ns::kEnumVariantPin);
        int variant = -1;
        for (size_t i = 0; i < def->variants.size(); ++i)
            if (def->variants[i].name == chosen)
                variant = static_cast<int>(i);
        if (variant < 0)
        {
            error = chosen.empty() ? juce::String("choose its value in Properties.") : "the enum has no value " + juce::String(chosen) + " any more.";
            return false;
        }
        auto made = std::make_shared<StructValue>();
        made->type = def->name;
        made->variant = variant;
        for (const auto& pin : node.Inputs())
            if (pin.name != ns::kEnumTypePin && pin.name != ns::kEnumVariantPin)
                takeMember(graph, context, pin, *made);
        context.valueOutputs[ns::kEnumValuePin] = static_cast<std::int64_t>(variant);
        context.structOutputs[ns::kEnumValuePin] = std::move(made);
        return true;
    }

    // Match: the value coming in - wired with what it carries, or just its number (then it carries the defaults).
    StructPtr incoming;
    if (auto wired = context.structs.find(ns::kEnumValuePin); wired != context.structs.end() && wired->second != nullptr)
        incoming = wired->second;
    else
    {
        const auto* index = context.setting(ns::kEnumValuePin) != nullptr ? std::get_if<std::int64_t>(context.setting(ns::kEnumValuePin)) : nullptr;
        incoming = enumWithDefaults(graph, *def, index != nullptr ? static_cast<int>(*index) : 0, nullptr, 0);
    }
    const auto& chosenName = def->variants[static_cast<size_t>(juce::jlimit(0, static_cast<int>(def->variants.size()) - 1, incoming->variant))].name;

    // Every variant's fields have pins; only the chosen variant's carry anything (like a Route's unchosen outputs).
    for (const auto& variant : def->variants)
        for (const auto& field : variant.fields)
        {
            const auto pinName = ns::MatchPinName(variant.name, field.name);
            const ns::Pin* out = nullptr;
            for (const auto& pin : node.Outputs())
                if (pin.name == pinName)
                    out = &pin;
            if (out == nullptr)
                continue;
            if (variant.name != chosenName)
            {
                if (context.wants(pinName))
                {
                    error = "Not chosen - the value is " + juce::String(chosenName) + ".";
                    return false;
                }
                continue;
            }
            if (! giveMember(*incoming, ns::StructMemberPinName(field.name), *out, context, outputs, error))
                return false;
        }
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
