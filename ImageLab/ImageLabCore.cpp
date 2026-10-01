#include "ImageLabCore.h"
#include "ImageLabFrustSources.h"

#include <creation/frust/PluginRuntime.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace image_lab
{
namespace
{
constexpr const char* coreKey = "image_lab_core";

using CompositeFunction = std::int64_t (*)(float*, const float*, std::int64_t, std::int64_t, float);

float srgbToLinear(float c)
{
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float linearToSrgb(float c)
{
    c = juce::jlimit(0.0f, 1.0f, c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}
}

juce::StringArray blendModeNames()
{
    return { "Normal", "Multiply", "Screen", "Overlay", "Add", "Subtract", "Darken", "Lighten", "Difference" };
}

Layer layerFromImage(const juce::Image& image, const juce::String& name)
{
    Layer layer;
    layer.name = name;
    if (! image.isValid())
        return layer;

    const auto argb = image.convertedToFormat(juce::Image::ARGB);
    layer.width = argb.getWidth();
    layer.height = argb.getHeight();
    layer.pixels.resize(static_cast<size_t>(layer.width) * static_cast<size_t>(layer.height) * 4);

    // 8-bit sRGB -> linear by table: 256 values, not a pow per channel.
    static const auto toLinear = [] {
        std::array<float, 256> t {};
        for (int i = 0; i < 256; ++i)
            t[static_cast<size_t>(i)] = srgbToLinear(static_cast<float>(i) / 255.0f);
        return t;
    }();

    const juce::Image::BitmapData data(argb, juce::Image::BitmapData::readOnly);
    size_t o = 0;
    for (int y = 0; y < layer.height; ++y)
    {
        for (int x = 0; x < layer.width; ++x)
        {
            const auto colour = data.getPixelColour(x, y); // un-premultiplied
            layer.pixels[o++] = toLinear[colour.getRed()];
            layer.pixels[o++] = toLinear[colour.getGreen()];
            layer.pixels[o++] = toLinear[colour.getBlue()];
            layer.pixels[o++] = colour.getFloatAlpha();
        }
    }
    return layer;
}

juce::Image imageFromPixels(const std::vector<float>& pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || pixels.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        return {};

    juce::Image image(juce::Image::ARGB, width, height, true);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    size_t o = 0;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto r = linearToSrgb(pixels[o]);
            const auto g = linearToSrgb(pixels[o + 1]);
            const auto b = linearToSrgb(pixels[o + 2]);
            const auto a = juce::jlimit(0.0f, 1.0f, pixels[o + 3]);
            data.setPixelColour(x, y, juce::Colour::fromFloatRGBA(r, g, b, a));
            o += 4;
        }
    }
    return image;
}

class Compositor::Implementation final
{
public:
    Implementation() : runtime("creation-texture")
    {
        ::frust::CompileRequest request;
        request.sources.push_back({ "image_lab_core.frust",
                                    std::string(ImageLabFrust::image_lab_core_frust, ImageLabFrust::image_lab_core_frustSize) });

        std::string loadError;
        if (! runtime.loadSource(coreKey, request, loadError))
        {
            error = "Image Lab's FRust routines did not compile: " + juce::String(loadError);
            return;
        }

        composite = reinterpret_cast<CompositeFunction>(runtime.getFunction(coreKey, "image_lab_composite"));
        if (composite == nullptr)
            error = "Image Lab's FRust routines are missing image_lab_composite().";
    }

    creation::frust::PluginRuntime runtime;
    CompositeFunction composite = nullptr;
    juce::String error;
};

Compositor::Compositor() : implementation(std::make_unique<Implementation>()) {}
Compositor::~Compositor() = default;

bool Compositor::isReady() const noexcept
{
    return implementation->composite != nullptr;
}

juce::String Compositor::getError() const
{
    return implementation->error;
}

bool Compositor::composite(float* canvas, const float* layer, std::int64_t pixelCount, BlendMode mode, float opacity, juce::String& error)
{
    if (! isReady())
    {
        error = getError();
        return false;
    }
    if (implementation->composite(canvas, layer, pixelCount, static_cast<std::int64_t>(mode), opacity) != 1)
    {
        error = "Image Lab's FRust compositor rejected the pixels.";
        return false;
    }
    return true;
}

bool flatten(const std::vector<const Layer*>& bottomToTop, int width, int height, Compositor& compositor,
             std::vector<float>& outPixels, juce::String& error)
{
    const size_t pixelCount = static_cast<size_t>(juce::jmax(0, width)) * static_cast<size_t>(juce::jmax(0, height));
    outPixels.assign(pixelCount * 4, 0.0f);

    std::vector<float> placed;
    for (const auto* layer : bottomToTop)
    {
        if (layer == nullptr || ! layer->visible || ! layer->isValid())
            continue;

        const float* source = layer->pixels.data();
        if (layer->width != width || layer->height != height)
        {
            // Place at the top-left of the canvas, cropped or padded with transparency.
            placed.assign(pixelCount * 4, 0.0f);
            const int rows = juce::jmin(height, layer->height);
            const int columns = juce::jmin(width, layer->width);
            for (int y = 0; y < rows; ++y)
                std::copy_n(layer->pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(layer->width) * 4,
                            static_cast<size_t>(columns) * 4,
                            placed.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 4);
            source = placed.data();
        }

        if (! compositor.composite(outPixels.data(), source, static_cast<std::int64_t>(pixelCount), layer->mode, layer->opacity, error))
            return false;
    }
    return true;
}
}
