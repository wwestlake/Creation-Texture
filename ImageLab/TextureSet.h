#pragma once

#include <juce_graphics/juce_graphics.h>

#include "SurfaceMaps.h"

#include <vector>

// A texture set ("Enhanced Texture"): one project asset that keeps all of a surface's maps together, so a game
// engine or other tool loads it as one thing. The asset is a manifest (<name>.texset.json) naming each map's role,
// file, colour space and bit depth, the source photo, and the exact settings that made it (so it can be remade).
// The map images live beside it in <name>.texset/. No UI; the app writes the files into the project.
namespace texture_set
{
constexpr const char* formatName = "djehuti-texture-set";
constexpr int formatVersion = 1;

struct File
{
    juce::String logicalPath;
    juce::MemoryBlock bytes;
};

struct Pack
{
    File manifest;             // the asset itself
    std::vector<File> maps;    // the member images
    int revision = 1;          // which version of the texture set this save is
};

// Builds the manifest and encodes every map. basePath is the folder for the pack, e.g. "Assets/Source/"; the pack
// is named after `name`. Returns false with an error if an image cannot be encoded.
//
// Versions: pass the set's current manifest as previousManifest when saving over an existing set. The new save is
// the next revision; its maps go in <name>.texset/v<revision>/ so earlier versions' files are never overwritten,
// and the previous version (its settings, source and map files) moves into the manifest's "history".
bool build(const surface_maps::Maps& maps, const surface_maps::Settings& settings, const juce::String& name,
           const juce::String& basePath, const juce::String& sourceAsset, Pack& out, juce::String& error,
           const surface_maps::Progress& progress = {}, const juce::var& previousManifest = {});

// The logical path of a texture set's manifest, from its name.
juce::String manifestPathFor(const juce::String& basePath, const juce::String& name);

// Images for display and 8-bit files. "Raw" keeps the stored values (data maps: normal, height, occlusion ...);
// "srgb" converts linear light to sRGB (colour maps).
juce::Image grayToImage(const std::vector<float>& gray, int width, int height);
juce::Image rgbaToRawImage(const std::vector<float>& rgba, int width, int height);
juce::Image rgbaToSrgbImage(const std::vector<float>& rgba, int width, int height);
juce::Image packOrm(const surface_maps::Maps& maps);

// A 16-bit greyscale PNG (JUCE's PNG writer only writes 8-bit). Values are clamped to 0..1.
bool encodeGray16Png(const std::vector<float>& gray, int width, int height, juce::MemoryBlock& out);

juce::String slugFor(const juce::String& name);
}
