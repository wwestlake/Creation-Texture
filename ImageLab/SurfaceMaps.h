#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

// Surface Map mode's maths pipeline: CrazyBump-style maps from one photo - height, normal, occlusion, specular /
// roughness and a de-lit diffuse. The pixel routines are FRust (Frust/surface_maps.frust, embedded and compiled
// from memory); this class chains them. No UI. See docs/REQUIREMENTS.md.
//
// Buffers are 32-bit float, linear light: "gray" = one float per pixel, "rgba" = four. Maps tile (edges wrap).
namespace surface_maps
{
struct Settings
{
    bool sunken = false;         // CrazyBump's "which one looks right": raised (bright = high) or sunken
    float intensity = 1.0f;      // normal map strength
    float sharpen = 0.0f;        // 0..2, unsharp mask on the source before height
    float noiseRemoval = 0.0f;   // 0..8, blur radius (at 512 px) on the source before height
    float fine = 0.6f;           // detail band weights, 0..1
    float medium = 0.5f;
    float large = 0.4f;
    float veryLarge = 0.3f;
    float huge = 0.2f;
    bool directX = false;        // flip green (DirectX normal convention)
    float occlusion = 1.0f;      // 0..2
    float specularLevel = 0.5f;  // 0..1
    float specularContrast = 4.0f;
    float delight = 0.8f;        // 0..1, how much large-scale lighting to remove from the diffuse
    float metallic = 0.0f;       // constant metallic for the packed map

    juce::var toVar() const;
    static Settings fromVar(const juce::var& v);
};

struct Maps
{
    int width = 0;
    int height = 0;
    std::vector<float> heightMap;   // gray, 0..1
    std::vector<float> normal;      // rgba, tangent space encoded 0.5 + 0.5 n
    std::vector<float> occlusion;   // gray, 0..1
    std::vector<float> specular;    // gray, 0..1
    std::vector<float> roughness;   // gray, 0..1 (1 - specular)
    std::vector<float> diffuse;     // rgba, linear, lighting removed
    float metallic = 0.0f;

    bool isValid() const noexcept { return width > 0 && height > 0 && heightMap.size() == static_cast<size_t>(width) * static_cast<size_t>(height); }
};

// Which maps to make. Height is made whenever normal or occlusion needs it; anything not wanted is skipped.
struct Wanted
{
    bool height = true;
    bool normal = true;
    bool occlusion = true;
    bool specular = true;   // specular and roughness
    bool diffuse = true;
};

// Reports how far a long job has got (0..1) and the step it is on. Called on the working thread.
using Progress = std::function<void(float fraction, const juce::String& step)>;

class Engine final
{
public:
    Engine();
    ~Engine();

    bool isReady() const noexcept;
    juce::String getError() const;

    // source: rgba linear, width * height pixels. Safe to call from a background thread (one call at a time).
    bool compute(const std::vector<float>& source, int width, int height, const Settings& settings, Maps& out, juce::String& error,
                 const Progress& progress = {}, const Wanted& wanted = {});

    // The raw routines, exposed for tests.
    bool boxBlur(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, juce::String& error);
    bool normalFromHeight(const std::vector<float>& heightMap, std::vector<float>& rgba, int width, int height, float strength, bool directX, juce::String& error);
    bool occlusionStep(std::vector<float>& ao, const std::vector<float>& heightMap, const std::vector<float>& blurred, float strength, juce::String& error);
    bool delight(const std::vector<float>& rgbaIn, const std::vector<float>& lighting, std::vector<float>& rgbaOut, float mean, float amount, juce::String& error);
    bool specular(const std::vector<float>& gray, const std::vector<float>& blurred, std::vector<float>& out, float level, float contrast, juce::String& error);
    bool luminance(const std::vector<float>& rgba, std::vector<float>& gray, juce::String& error);
    bool normalize(std::vector<float>& gray, juce::String& error);

private:
    bool blurPass(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, bool horizontal);
    bool gaussianish(const std::vector<float>& src, std::vector<float>& dst, int width, int height, int radius, juce::String& error);

    class Implementation;
    std::unique_ptr<Implementation> implementation;
};
}
