#pragma once
#include <JuceHeader.h>
#include <string>
#include <vector>

struct TextureFrameImageSlot {
    std::string uniformName;
    juce::Image image;
};

struct TextureFrameSnapshot {
    juce::Colour debugColour { juce::Colours::black };
    std::string generatedGlsl;
    std::vector<TextureFrameImageSlot> imageSlots;
};
