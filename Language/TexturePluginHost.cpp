#include "TexturePluginHost.h"

#include <creation/frust/PluginRuntime.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{
extern "C" double suite_texture_pow(double value, double exponent)
{
    return std::pow(value, exponent);
}

using ApplyAdjustmentsFunction = int64_t (*)(uint8_t*, int64_t, int64_t, int64_t, double, double, double, double);
}

namespace creation_texture::language
{
class TexturePluginHost::Implementation final
{
public:
    Implementation()
        : runtime("creation-texture")
    {
        runtime.registerHostFunction("suite_texture_pow", reinterpret_cast<void*>(&suite_texture_pow));

        const auto pluginFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                                    .getParentDirectory()
                                    .getChildFile("plugins")
                                    .getChildFile("TextureAdjustments.frust");
        if (! pluginFile.existsAsFile())
        {
            loadError = "Texture Adjustments plugin is missing from " + pluginFile.getParentDirectory().getFullPathName();
            return;
        }

        std::string error;
        if (! runtime.load(pluginFile.getFullPathName().toStdString(), error))
        {
            loadError = juce::String(error);
            return;
        }

        apply = reinterpret_cast<ApplyAdjustmentsFunction>(runtime.getFunction("texture_apply_adjustments"));
        if (apply == nullptr)
            loadError = "Texture Adjustments plugin is missing texture_apply_adjustments().";
    }

    creation::frust::PluginRuntime runtime;
    ApplyAdjustmentsFunction apply = nullptr;
    juce::String loadError;
};

TexturePluginHost::TexturePluginHost()
    : implementation(std::make_unique<Implementation>())
{
}

TexturePluginHost::~TexturePluginHost() = default;

juce::Image TexturePluginHost::applyAdjustments(const juce::Image& source,
                                                float brightness,
                                                float contrast,
                                                float saturation,
                                                float gamma,
                                                juce::String& error)
{
    if (source.isNull())
        return {};

    if (! isReady())
    {
        error = implementation->loadError;
        return {};
    }

    const auto argb = source.convertedToFormat(juce::Image::ARGB);
    const int width = argb.getWidth();
    const int height = argb.getHeight();
    std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 4));

    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto colour = argb.getPixelAt(x, y);
            const auto offset = static_cast<size_t>((y * width + x) * 4);
            pixels[offset] = colour.getRed();
            pixels[offset + 1] = colour.getGreen();
            pixels[offset + 2] = colour.getBlue();
            pixels[offset + 3] = colour.getAlpha();
        }
    }

    if (implementation->apply(pixels.data(), width, height, width * 4,
                              brightness, contrast, saturation, gamma) == 0)
    {
        error = "Texture Adjustments plugin rejected the image buffer.";
        return {};
    }

    juce::Image adjusted(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto offset = static_cast<size_t>((y * width + x) * 4);
            adjusted.setPixelAt(x, y, juce::Colour(pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]));
        }
    }

    return adjusted;
}

bool TexturePluginHost::isReady() const noexcept
{
    return implementation != nullptr && implementation->apply != nullptr;
}
}
