#include "TextureSet.h"

#include <array>
#include <cmath>

namespace texture_set
{
namespace
{
std::uint8_t toByte(float v)
{
    return static_cast<std::uint8_t>(juce::jlimit(0, 255, juce::roundToInt(juce::jlimit(0.0f, 1.0f, v) * 255.0f)));
}

float linearToSrgb(float c)
{
    c = juce::jlimit(0.0f, 1.0f, c);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

std::uint32_t crc32(const std::uint8_t* data, size_t size, std::uint32_t crc = 0xffffffffu)
{
    static const auto table = [] {
        std::array<std::uint32_t, 256> t {};
        for (std::uint32_t n = 0; n < 256; ++n)
        {
            std::uint32_t c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
    return crc;
}

void writeBigEndian32(juce::MemoryOutputStream& out, std::uint32_t v)
{
    const std::uint8_t b[4] = { static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
                                static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v) };
    out.write(b, 4);
}

void writeChunk(juce::MemoryOutputStream& out, const char* type, const void* data, size_t size)
{
    writeBigEndian32(out, static_cast<std::uint32_t>(size));
    juce::MemoryBlock body(type, 4);
    body.append(data, size);
    out.write(body.getData(), body.getSize());
    writeBigEndian32(out, crc32(static_cast<const std::uint8_t*>(body.getData()), body.getSize()) ^ 0xffffffffu);
}

bool encode8(const juce::Image& image, juce::MemoryBlock& out)
{
    juce::MemoryOutputStream stream(out, false);
    juce::PNGImageFormat format;
    return image.isValid() && format.writeImageToStream(image, stream);
}

juce::var mapEntry(const juce::String& path, const juce::String& colourSpace, int bits, const juce::String& channels)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("path", path);
    o->setProperty("colorSpace", colourSpace);
    o->setProperty("bits", bits);
    o->setProperty("channels", channels);
    return juce::var(o);
}
}

juce::String slugFor(const juce::String& name)
{
    auto slug = name.trim().toLowerCase().retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-_ ").replace(" ", "-");
    while (slug.contains("--"))
        slug = slug.replace("--", "-");
    slug = slug.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    return slug.isNotEmpty() ? slug : "texture";
}

juce::Image grayToImage(const std::vector<float>& gray, int width, int height)
{
    if (width <= 0 || height <= 0 || gray.size() < static_cast<size_t>(width) * static_cast<size_t>(height))
        return {};
    juce::Image image(juce::Image::RGB, width, height, false);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const auto v = toByte(gray[static_cast<size_t>(y * width + x)]);
            data.setPixelColour(x, y, juce::Colour(v, v, v));
        }
    return image;
}

juce::Image rgbaToRawImage(const std::vector<float>& rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        return {};
    juce::Image image(juce::Image::RGB, width, height, false);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t o = static_cast<size_t>(y * width + x) * 4;
            data.setPixelColour(x, y, juce::Colour(toByte(rgba[o]), toByte(rgba[o + 1]), toByte(rgba[o + 2])));
        }
    return image;
}

juce::Image rgbaToSrgbImage(const std::vector<float>& rgba, int width, int height)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4)
        return {};
    juce::Image image(juce::Image::RGB, width, height, false);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            const size_t o = static_cast<size_t>(y * width + x) * 4;
            data.setPixelColour(x, y, juce::Colour(toByte(linearToSrgb(rgba[o])), toByte(linearToSrgb(rgba[o + 1])),
                                                   toByte(linearToSrgb(rgba[o + 2]))));
        }
    return image;
}

juce::Image packOrm(const surface_maps::Maps& maps)
{
    if (! maps.isValid())
        return {};
    juce::Image image(juce::Image::RGB, maps.width, maps.height, false);
    juce::Image::BitmapData data(image, juce::Image::BitmapData::writeOnly);
    const auto metal = toByte(maps.metallic);
    for (int y = 0; y < maps.height; ++y)
        for (int x = 0; x < maps.width; ++x)
        {
            const size_t i = static_cast<size_t>(y * maps.width + x);
            data.setPixelColour(x, y, juce::Colour(toByte(maps.occlusion[i]), toByte(maps.roughness[i]), metal));
        }
    return image;
}

bool encodeGray16Png(const std::vector<float>& gray, int width, int height, juce::MemoryBlock& out)
{
    if (width <= 0 || height <= 0 || gray.size() < static_cast<size_t>(width) * static_cast<size_t>(height))
        return false;

    // Raw scanlines: a filter byte (0 = none), then big-endian 16-bit samples.
    juce::MemoryBlock raw;
    {
        juce::MemoryOutputStream rows(raw, false);
        for (int y = 0; y < height; ++y)
        {
            rows.writeByte(0);
            for (int x = 0; x < width; ++x)
            {
                const auto v = static_cast<std::uint16_t>(juce::roundToInt(juce::jlimit(0.0f, 1.0f, gray[static_cast<size_t>(y * width + x)]) * 65535.0f));
                rows.writeByte(static_cast<char>(v >> 8));
                rows.writeByte(static_cast<char>(v & 0xff));
            }
        }
    }

    juce::MemoryBlock compressed;
    {
        juce::MemoryOutputStream sink(compressed, false);
        juce::GZIPCompressorOutputStream zlib(sink, 9); // zlib-wrapped deflate, as PNG requires
        zlib.write(raw.getData(), raw.getSize());
        zlib.flush();
    }

    out.reset();
    juce::MemoryOutputStream png(out, false);
    const std::uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a };
    png.write(signature, 8);

    std::uint8_t header[13];
    for (int i = 0; i < 4; ++i)
    {
        header[i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(width) >> (24 - 8 * i));
        header[4 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(height) >> (24 - 8 * i));
    }
    header[8] = 16; // bit depth
    header[9] = 0;  // greyscale
    header[10] = header[11] = header[12] = 0;
    writeChunk(png, "IHDR", header, sizeof(header));
    writeChunk(png, "IDAT", compressed.getData(), compressed.getSize());
    writeChunk(png, "IEND", nullptr, 0);
    return true;
}

bool build(const surface_maps::Maps& maps, const surface_maps::Settings& settings, const juce::String& name,
           const juce::String& basePath, const juce::String& sourceAsset, Pack& out, juce::String& error)
{
    if (! maps.isValid())
    {
        error = "There are no maps to save yet.";
        return false;
    }

    const auto slug = slugFor(name);
    const auto folder = basePath + slug + ".texset/";
    out = {};
    out.manifest.logicalPath = basePath + slug + ".texset.json";

    auto* mapsObject = new juce::DynamicObject();

    auto add = [&](const char* role, const char* file, const juce::String& colourSpace, int bits, const char* channels,
                   bool encoded, juce::MemoryBlock bytes) {
        if (! encoded)
        {
            error = juce::String("Could not encode the ") + role + " map.";
            return false;
        }
        out.maps.push_back({ folder + file, std::move(bytes) });
        mapsObject->setProperty(role, mapEntry(folder + file, colourSpace, bits, channels));
        return true;
    };

    juce::MemoryBlock bytes;
    bool ok = encode8(rgbaToSrgbImage(maps.diffuse, maps.width, maps.height), bytes);
    if (! add("baseColor", "basecolor.png", "srgb", 8, "rgb", ok, bytes)) return false;

    bytes.reset();
    ok = encode8(rgbaToRawImage(maps.normal, maps.width, maps.height), bytes);
    if (! add("normal", "normal.png", settings.directX ? "linear-tangent-directx" : "linear-tangent-opengl", 8, "rgb", ok, bytes)) return false;

    bytes.reset();
    ok = encodeGray16Png(maps.heightMap, maps.width, maps.height, bytes);
    if (! add("height", "height.png", "linear", 16, "gray", ok, bytes)) return false;

    bytes.reset();
    ok = encode8(grayToImage(maps.occlusion, maps.width, maps.height), bytes);
    if (! add("occlusion", "occlusion.png", "linear", 8, "gray", ok, bytes)) return false;

    bytes.reset();
    ok = encode8(grayToImage(maps.roughness, maps.width, maps.height), bytes);
    if (! add("roughness", "roughness.png", "linear", 8, "gray", ok, bytes)) return false;

    bytes.reset();
    ok = encode8(packOrm(maps), bytes);
    if (! add("occlusionRoughnessMetallic", "orm.png", "linear", 8, "r=occlusion,g=roughness,b=metallic", ok, bytes)) return false;

    auto* manifest = new juce::DynamicObject();
    manifest->setProperty("format", formatName);
    manifest->setProperty("version", formatVersion);
    manifest->setProperty("name", name);
    manifest->setProperty("width", maps.width);
    manifest->setProperty("height", maps.height);
    manifest->setProperty("tiling", "wrap");
    manifest->setProperty("metallic", maps.metallic);
    manifest->setProperty("source", sourceAsset);
    manifest->setProperty("madeBy", "Djehuti Texture - Surface Map");
    manifest->setProperty("settings", settings.toVar());
    manifest->setProperty("maps", juce::var(mapsObject));

    const auto json = juce::JSON::toString(juce::var(manifest), false);
    out.manifest.bytes = juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8());
    return true;
}
}
