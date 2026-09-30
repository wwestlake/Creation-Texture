#include "SurfaceMaps.h"
#include "ImageLabFrustSources.h"

#include <creation/frust/PluginRuntime.h>

#include <cmath>
#include <cstdint>

namespace
{
extern "C" float sm_host_sqrt(float v) { return std::sqrt(v); }
extern "C" float sm_host_i64_to_f32(std::int64_t v) { return static_cast<float>(v); }

constexpr const char* key = "surface_maps";

using LuminanceFn = std::int64_t (*)(const float*, float*, std::int64_t);
using BlurFn = std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, std::int64_t);
using AddBandFn = std::int64_t (*)(float*, const float*, const float*, std::int64_t, float);
using UnsharpFn = std::int64_t (*)(const float*, const float*, float*, std::int64_t, float);
using MinMaxFn = std::int64_t (*)(const float*, std::int64_t, float*);
using NormalizeFn = std::int64_t (*)(float*, std::int64_t, float, float);
using InvertFn = std::int64_t (*)(float*, std::int64_t);
using NormalFn = std::int64_t (*)(const float*, float*, std::int64_t, std::int64_t, float, std::int64_t);
using OcclusionFn = std::int64_t (*)(float*, const float*, const float*, std::int64_t, float);
using FillFn = std::int64_t (*)(float*, std::int64_t, float);
using DelightFn = std::int64_t (*)(const float*, const float*, float*, std::int64_t, float, float);
using SpecularFn = std::int64_t (*)(const float*, const float*, float*, std::int64_t, float, float);
}

namespace surface_maps
{
juce::var Settings::toVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty("sunken", sunken);
    o->setProperty("intensity", intensity);
    o->setProperty("sharpen", sharpen);
    o->setProperty("noiseRemoval", noiseRemoval);
    o->setProperty("fine", fine);
    o->setProperty("medium", medium);
    o->setProperty("large", large);
    o->setProperty("veryLarge", veryLarge);
    o->setProperty("huge", huge);
    o->setProperty("directX", directX);
    o->setProperty("occlusion", occlusion);
    o->setProperty("specularLevel", specularLevel);
    o->setProperty("specularContrast", specularContrast);
    o->setProperty("delight", delight);
    o->setProperty("metallic", metallic);
    return juce::var(o);
}

Settings Settings::fromVar(const juce::var& v)
{
    Settings s;
    auto f = [&v](const char* name, float fallback) { return v.hasProperty(name) ? static_cast<float>(v[name]) : fallback; };
    s.sunken = v.hasProperty("sunken") ? static_cast<bool>(v["sunken"]) : s.sunken;
    s.intensity = f("intensity", s.intensity);
    s.sharpen = f("sharpen", s.sharpen);
    s.noiseRemoval = f("noiseRemoval", s.noiseRemoval);
    s.fine = f("fine", s.fine);
    s.medium = f("medium", s.medium);
    s.large = f("large", s.large);
    s.veryLarge = f("veryLarge", s.veryLarge);
    s.huge = f("huge", s.huge);
    s.directX = v.hasProperty("directX") ? static_cast<bool>(v["directX"]) : s.directX;
    s.occlusion = f("occlusion", s.occlusion);
    s.specularLevel = f("specularLevel", s.specularLevel);
    s.specularContrast = f("specularContrast", s.specularContrast);
    s.delight = f("delight", s.delight);
    s.metallic = f("metallic", s.metallic);
    return s;
}

class Engine::Implementation final
{
public:
    Implementation() : runtime("creation-texture")
    {
        runtime.registerHostFunction("sm_host_sqrt", reinterpret_cast<void*>(&sm_host_sqrt));
        runtime.registerHostFunction("sm_host_i64_to_f32", reinterpret_cast<void*>(&sm_host_i64_to_f32));

        ::frust::CompileRequest request;
        request.sources.push_back({ "surface_maps.frust",
                                    std::string(ImageLabFrust::surface_maps_frust, ImageLabFrust::surface_maps_frustSize) });
        std::string loadError;
        if (! runtime.loadSource(key, request, loadError))
        {
            error = "Surface map FRust routines did not compile: " + juce::String(loadError);
            return;
        }

        auto get = [this](const char* name) { return runtime.getFunction(key, name); };
        luminance = reinterpret_cast<LuminanceFn>(get("sm_luminance"));
        blurH = reinterpret_cast<BlurFn>(get("sm_box_blur_h"));
        blurV = reinterpret_cast<BlurFn>(get("sm_box_blur_v"));
        addBand = reinterpret_cast<AddBandFn>(get("sm_add_band"));
        unsharp = reinterpret_cast<UnsharpFn>(get("sm_unsharp"));
        minMax = reinterpret_cast<MinMaxFn>(get("sm_min_max"));
        normalize = reinterpret_cast<NormalizeFn>(get("sm_normalize"));
        invert = reinterpret_cast<InvertFn>(get("sm_invert"));
        normal = reinterpret_cast<NormalFn>(get("sm_normal_from_height"));
        occlusion = reinterpret_cast<OcclusionFn>(get("sm_occlusion_step"));
        fill = reinterpret_cast<FillFn>(get("sm_fill"));
        delight = reinterpret_cast<DelightFn>(get("sm_delight"));
        specular = reinterpret_cast<SpecularFn>(get("sm_specular"));

        if (! (luminance && blurH && blurV && addBand && unsharp && minMax && normalize && invert && normal && occlusion && fill && delight && specular))
            error = "Surface map FRust routines are incomplete.";
    }

    creation::frust::PluginRuntime runtime;
    LuminanceFn luminance = nullptr;
    BlurFn blurH = nullptr;
    BlurFn blurV = nullptr;
    AddBandFn addBand = nullptr;
    UnsharpFn unsharp = nullptr;
    MinMaxFn minMax = nullptr;
    NormalizeFn normalize = nullptr;
    InvertFn invert = nullptr;
    NormalFn normal = nullptr;
    OcclusionFn occlusion = nullptr;
    FillFn fill = nullptr;
    DelightFn delight = nullptr;
    SpecularFn specular = nullptr;
    juce::String error;
};

Engine::Engine() : implementation(std::make_unique<Implementation>()) {}
Engine::~Engine() = default;

bool Engine::isReady() const noexcept { return implementation->error.isEmpty(); }
juce::String Engine::getError() const { return implementation->error; }

bool Engine::blurPass(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, bool horizontal)
{
    dst.resize(src.size());
    const auto fn = horizontal ? implementation->blurH : implementation->blurV;
    return fn(src.data(), dst.data(), width, height, radius) == 1;
}

bool Engine::boxBlur(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    std::vector<float> tmp;
    if (! blurPass(src, tmp, width, height, radius, true) || ! blurPass(tmp, dst, width, height, radius, false))
    {
        error = "Blur radius " + juce::String(radius) + " is too large for a " + juce::String(width) + " x " + juce::String(height) + " image.";
        return false;
    }
    return true;
}

// Three box blurs approximate a Gaussian.
bool Engine::gaussianish(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, juce::String& error)
{
    radius = juce::jlimit(0, juce::jmax(0, (juce::jmin(width, height) - 1) / 2), radius);
    std::vector<float> a, b;
    return boxBlur(src, a, width, height, radius, error) && boxBlur(a, b, width, height, radius, error)
        && boxBlur(b, dst, width, height, radius, error);
}

bool Engine::luminance(const std::vector<float>& rgba, std::vector<float>& gray, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    gray.resize(rgba.size() / 4);
    return implementation->luminance(rgba.data(), gray.data(), static_cast<std::int64_t>(gray.size())) == 1;
}

bool Engine::normalize(std::vector<float>& gray, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    float range[2] = { 0.0f, 0.0f };
    const auto count = static_cast<std::int64_t>(gray.size());
    return implementation->minMax(gray.data(), count, range) == 1 && implementation->normalize(gray.data(), count, range[0], range[1]) == 1;
}

bool Engine::normalFromHeight(const std::vector<float>& heightMap, std::vector<float>& rgba, int width, int height, float strength, bool directX, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    rgba.resize(heightMap.size() * 4);
    if (implementation->normal(heightMap.data(), rgba.data(), width, height, strength, directX ? 1 : 0) != 1)
    {
        error = "A normal map needs an image at least 3 x 3.";
        return false;
    }
    return true;
}

bool Engine::occlusionStep(std::vector<float>& ao, const std::vector<float>& heightMap, const std::vector<float>& blurred, float strength, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    return implementation->occlusion(ao.data(), heightMap.data(), blurred.data(), static_cast<std::int64_t>(ao.size()), strength) == 1;
}

bool Engine::delight(const std::vector<float>& rgbaIn, const std::vector<float>& lighting, std::vector<float>& rgbaOut, float mean, float amount, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    rgbaOut.resize(rgbaIn.size());
    return implementation->delight(rgbaIn.data(), lighting.data(), rgbaOut.data(), static_cast<std::int64_t>(lighting.size()), mean, amount) == 1;
}

bool Engine::specular(const std::vector<float>& gray, const std::vector<float>& blurred, std::vector<float>& out, float level, float contrast, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    out.resize(gray.size());
    return implementation->specular(gray.data(), blurred.data(), out.data(), static_cast<std::int64_t>(gray.size()), level, contrast) == 1;
}

bool Engine::compute(const std::vector<float>& source, int width, int height, const Settings& s, Maps& out, juce::String& error)
{
    if (! isReady()) { error = getError(); return false; }
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (width < 3 || height < 3 || source.size() < count * 4)
    {
        error = "The source image is too small.";
        return false;
    }

    // Radii scale with the image, tuned at 512 px.
    const float scale = static_cast<float>(juce::jmax(width, height)) / 512.0f;
    auto radius = [scale](float at512) { return juce::jmax(1, juce::roundToInt(at512 * scale)); };

    std::vector<float> gray;
    if (! luminance(source, gray, error))
        return false;

    if (s.noiseRemoval > 0.0f)
    {
        std::vector<float> smoothed;
        if (! gaussianish(gray, smoothed, width, height, radius(s.noiseRemoval), error))
            return false;
        gray.swap(smoothed);
    }
    if (s.sharpen > 0.0f)
    {
        std::vector<float> blurred;
        if (! gaussianish(gray, blurred, width, height, 1, error))
            return false;
        implementation->unsharp(gray.data(), blurred.data(), gray.data(), static_cast<std::int64_t>(count), s.sharpen);
    }

    // Detail bands: the differences between successive blur levels, fine to huge. Larger bands carry larger
    // shapes, so they are weighted up; the sliders scale each band.
    const float radii[5] = { 1.0f, 3.0f, 8.0f, 20.0f, 48.0f };
    const float weights[5] = { s.fine, s.medium, s.large, s.veryLarge, s.huge };
    std::vector<std::vector<float>> levels(6);
    levels[0] = gray;
    for (int i = 0; i < 5; ++i)
        if (! gaussianish(gray, levels[static_cast<size_t>(i + 1)], width, height, radius(radii[i]), error))
            return false;

    out.width = width;
    out.height = height;
    out.heightMap.assign(count, 0.0f);
    for (int i = 0; i < 5; ++i)
        implementation->addBand(out.heightMap.data(), levels[static_cast<size_t>(i)].data(), levels[static_cast<size_t>(i + 1)].data(),
                                static_cast<std::int64_t>(count), weights[i] * static_cast<float>(i + 1));
    if (! normalize(out.heightMap, error))
        return false;
    if (s.sunken)
        implementation->invert(out.heightMap.data(), static_cast<std::int64_t>(count));

    if (! normalFromHeight(out.heightMap, out.normal, width, height, s.intensity * 8.0f * scale, s.directX, error))
        return false;

    out.occlusion.assign(count, 1.0f);
    for (float at512 : { 3.0f, 8.0f, 20.0f })
    {
        std::vector<float> blurredHeight;
        if (! gaussianish(out.heightMap, blurredHeight, width, height, radius(at512), error)
            || ! occlusionStep(out.occlusion, out.heightMap, blurredHeight, s.occlusion * 4.0f, error))
            return false;
    }

    if (! specular(gray, levels[2], out.specular, s.specularLevel, s.specularContrast, error))
        return false;
    out.roughness.resize(count);
    for (size_t i = 0; i < count; ++i)
        out.roughness[i] = 1.0f - out.specular[i];

    double sum = 0.0;
    for (float v : levels[0])
        sum += v;
    const float mean = static_cast<float>(sum / static_cast<double>(count));
    std::vector<float> lighting;
    if (! gaussianish(gray, lighting, width, height, radius(96.0f), error)
        || ! delight(source, lighting, out.diffuse, mean, s.delight, error))
        return false;

    out.metallic = s.metallic;
    return true;
}
}
