#pragma once

#include <juce_graphics/juce_graphics.h>

#include <memory>
#include <vector>

// Image Lab's layer model and pixel work, with no UI. See docs/REQUIREMENTS.md sections 5-8.
//
// Pixels are 32-bit float, linear light, straight (not premultiplied) RGBA - full precision while working. Images
// are converted from sRGB when they come in and back to sRGB when shown or saved.
namespace image_lab
{
// Same numbering as image_lab_composite() in Frust/image_lab_core.frust.
enum class BlendMode
{
    normal = 0,
    multiply,
    screen,
    overlay,
    add,
    subtract,
    darken,
    lighten,
    difference
};

juce::StringArray blendModeNames();

struct Layer
{
    juce::String name;
    // The project image it was made from, if any - for provenance when saving.
    juce::String sourceAsset;
    bool visible = true;
    float opacity = 1.0f;
    BlendMode mode = BlendMode::normal;
    int width = 0;
    int height = 0;
    std::vector<float> pixels; // width * height * 4

    bool isValid() const noexcept { return width > 0 && height > 0 && pixels.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 4; }
};

// sRGB juce::Image -> linear float layer pixels, and back.
Layer layerFromImage(const juce::Image& image, const juce::String& name);
juce::Image imageFromPixels(const std::vector<float>& pixels, int width, int height);

// Runs the FRust pixel routines. The FRust source is compiled into the app and compiled on first use - there is no
// plugin file on disk.
class Compositor final
{
public:
    Compositor();
    ~Compositor();

    bool isReady() const noexcept;
    juce::String getError() const;

    // Composites `layer` over `canvas` in place. Both hold `pixelCount` RGBA pixels.
    bool composite(float* canvas, const float* layer, std::int64_t pixelCount, BlendMode mode, float opacity, juce::String& error);

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};

// Flattens the visible layers, bottom (index 0) to top, onto a transparent canvas of the given size. Each layer is
// placed at the top-left, cropped or padded to the canvas.
bool flatten(const std::vector<const Layer*>& bottomToTop, int width, int height, Compositor& compositor,
             std::vector<float>& outPixels, juce::String& error);
}
