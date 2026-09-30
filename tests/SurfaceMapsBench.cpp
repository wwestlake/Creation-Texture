// Times the Surface Map save pipeline stage by stage at a real photo size. Developer tool, no UI.
//   DjehutiTextureSurfaceMapsBench.exe [width height]

#include "NoCrashDialogs.h"

#include <ImageLabCore.h>
#include <SurfaceMaps.h>
#include <TextureSet.h>

#include <iostream>

int main(int argc, char** argv)
{
    disableCrashDialogs();
    const int width = argc > 2 ? std::atoi(argv[1]) : 4256;
    const int height = argc > 2 ? std::atoi(argv[2]) : 2832;

    auto started = juce::Time::getMillisecondCounterHiRes();
    auto lap = [&started](const juce::String& what) {
        const auto now = juce::Time::getMillisecondCounterHiRes();
        std::cout << juce::String((now - started) / 1000.0, 2) << " s  " << what << std::endl;
        started = now;
    };

    juce::Image photo(juce::Image::RGB, width, height, false);
    {
        juce::Random random(42);
        juce::Image::BitmapData data(photo, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
            {
                const auto v = static_cast<juce::uint8>(120 + 60 * std::sin(x * 0.02) + random.nextInt(40));
                data.setPixelColour(x, y, juce::Colour(v, static_cast<juce::uint8>(v * 0.8), static_cast<juce::uint8>(v * 0.6)));
            }
    }
    lap("made a " + juce::String(width) + " x " + juce::String(height) + " test photo");

    const auto layer = image_lab::layerFromImage(photo, "bench");
    lap("layerFromImage (sRGB -> linear float)");

    surface_maps::Engine engine;
    lap("compile surface map FRust");

    juce::String step = "start";
    surface_maps::Maps maps;
    juce::String error;
    const bool ok = engine.compute(layer.pixels, layer.width, layer.height, {}, maps, error, [&](float, const juce::String& next) {
        lap(step);
        step = next;
    });
    lap(step);
    if (! ok)
    {
        std::cerr << "compute failed: " << error << "\n";
        return 1;
    }

    texture_set::Pack pack;
    step = "start encoding";
    if (! texture_set::build(maps, {}, "bench", "Assets/Source/", {}, pack, error, [&](float, const juce::String& next) {
            lap(step);
            step = next;
        }))
    {
        std::cerr << "build failed: " << error << "\n";
        return 1;
    }
    lap(step);

    juce::int64 bytes = 0;
    for (const auto& file : pack.maps)
        bytes += static_cast<juce::int64>(file.bytes.getSize());
    std::cout << "pack: " << pack.maps.size() << " maps, " << (bytes / (1024 * 1024)) << " MB" << std::endl;
    return 0;
}
