
#pragma once
#include <JuceHeader.h>
#include <string>
#include <vector>
#include "TextureCompiler.h"

struct TextureFrameImageSlot {
    std::string uniformName;
    juce::Image image;
};

struct TextureFrameSnapshot {
    juce::Colour debugColour { juce::Colours::black };
    std::string generatedGlsl;
    std::vector<creation_texture::TextureSlot> textures;
    std::vector<TextureFrameImageSlot> imageSlots;
};
