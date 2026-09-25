#pragma once

#include <juce_graphics/juce_graphics.h>

#include <memory>

namespace creation_texture::language
{
class TexturePluginHost final
{
public:
    TexturePluginHost();
    ~TexturePluginHost();

    TexturePluginHost(const TexturePluginHost&) = delete;
    TexturePluginHost& operator=(const TexturePluginHost&) = delete;

    juce::Image applyAdjustments(const juce::Image& source,
                                 float brightness,
                                 float contrast,
                                 float saturation,
                                 float gamma,
                                 juce::String& error);
    [[nodiscard]] bool isReady() const noexcept;

private:
    class Implementation;
    std::unique_ptr<Implementation> implementation;
};
}
