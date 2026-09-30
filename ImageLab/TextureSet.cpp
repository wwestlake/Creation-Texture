#include "TextureSet.h"

#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>

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

// A PNG from raw samples: colourType 0 = grey, 2 = RGB; bitDepth 8 or 16 (16-bit samples big-endian in `samples`).
// Each row uses the "Up" filter (difference from the row above), which compresses photos and smooth maps well.
bool encodePng(const std::vector<std::uint8_t>& samples, int width, int height, int colourType, int bitDepth, juce::MemoryBlock& out)
{
    const int channels = colourType == 2 ? 3 : 1;
    const size_t rowBytes = static_cast<size_t>(width) * static_cast<size_t>(channels) * static_cast<size_t>(bitDepth / 8);
    if (width <= 0 || height <= 0 || samples.size() < rowBytes * static_cast<size_t>(height))
        return false;

    std::vector<std::uint8_t> filtered(static_cast<size_t>(height) * (rowBytes + 1));
    for (int y = 0; y < height; ++y)
    {
        auto* dst = filtered.data() + static_cast<size_t>(y) * (rowBytes + 1);
        const auto* row = samples.data() + static_cast<size_t>(y) * rowBytes;
        dst[0] = y == 0 ? 0 : 2; // none for the first row, Up after
        if (y == 0)
            std::memcpy(dst + 1, row, rowBytes);
        else
        {
            const auto* above = row - rowBytes;
            for (size_t i = 0; i < rowBytes; ++i)
                dst[1 + i] = static_cast<std::uint8_t>(row[i] - above[i]);
        }
    }

    juce::MemoryBlock compressed;
    {
        juce::MemoryOutputStream sink(compressed, false);
        juce::GZIPCompressorOutputStream zlib(sink, 6); // zlib-wrapped deflate, as PNG requires
        zlib.write(filtered.data(), filtered.size());
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
    header[8] = static_cast<std::uint8_t>(bitDepth);
    header[9] = static_cast<std::uint8_t>(colourType);
    header[10] = header[11] = header[12] = 0;
    writeChunk(png, "IHDR", header, sizeof(header));
    writeChunk(png, "IDAT", compressed.getData(), compressed.getSize());
    writeChunk(png, "IEND", nullptr, 0);
    return true;
}

// linear 0..1 -> sRGB byte, by table (4096 steps) instead of a pow per value.
std::uint8_t srgbByte(float linear)
{
    static const auto table = [] {
        std::array<std::uint8_t, 4097> t {};
        for (int i = 0; i <= 4096; ++i)
            t[static_cast<size_t>(i)] = toByte(linearToSrgb(static_cast<float>(i) / 4096.0f));
        return t;
    }();
    return table[static_cast<size_t>(juce::jlimit(0, 4096, juce::roundToInt(juce::jlimit(0.0f, 1.0f, linear) * 4096.0f)))];
}

std::vector<std::uint8_t> grayBytes(const std::vector<float>& gray)
{
    std::vector<std::uint8_t> bytes(gray.size());
    for (size_t i = 0; i < gray.size(); ++i)
        bytes[i] = toByte(gray[i]);
    return bytes;
}

std::vector<std::uint8_t> gray16Bytes(const std::vector<float>& gray)
{
    std::vector<std::uint8_t> bytes(gray.size() * 2);
    for (size_t i = 0; i < gray.size(); ++i)
    {
        const auto v = static_cast<std::uint16_t>(juce::roundToInt(juce::jlimit(0.0f, 1.0f, gray[i]) * 65535.0f));
        bytes[i * 2] = static_cast<std::uint8_t>(v >> 8);
        bytes[i * 2 + 1] = static_cast<std::uint8_t>(v & 0xff);
    }
    return bytes;
}

std::vector<std::uint8_t> rgbBytes(const std::vector<float>& rgba, bool toSrgb)
{
    const size_t count = rgba.size() / 4;
    std::vector<std::uint8_t> bytes(count * 3);
    for (size_t i = 0; i < count; ++i)
        for (size_t c = 0; c < 3; ++c)
            bytes[i * 3 + c] = toSrgb ? srgbByte(rgba[i * 4 + c]) : toByte(rgba[i * 4 + c]);
    return bytes;
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
    return encodePng(gray16Bytes(gray), width, height, 0, 16, out);
}

bool build(const surface_maps::Maps& maps, const surface_maps::Settings& settings, const juce::String& name,
           const juce::String& basePath, const juce::String& sourceAsset, Pack& out, juce::String& error,
           const surface_maps::Progress& progress)
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

    // The six maps are independent, so they are encoded in parallel.
    struct Job
    {
        const char* role;
        const char* file;
        juce::String colourSpace;
        int bits;
        const char* channels;
        std::function<bool(juce::MemoryBlock&)> encode;
        juce::MemoryBlock bytes;
        bool ok = false;
    };
    const int w = maps.width, h = maps.height;
    std::vector<Job> jobs;
    jobs.push_back({ "baseColor", "basecolor.png", "srgb", 8, "rgb",
                     [&](juce::MemoryBlock& b) { return encodePng(rgbBytes(maps.diffuse, true), w, h, 2, 8, b); } });
    jobs.push_back({ "normal", "normal.png", settings.directX ? "linear-tangent-directx" : "linear-tangent-opengl", 8, "rgb",
                     [&](juce::MemoryBlock& b) { return encodePng(rgbBytes(maps.normal, false), w, h, 2, 8, b); } });
    jobs.push_back({ "height", "height.png", "linear", 16, "gray",
                     [&](juce::MemoryBlock& b) { return encodePng(gray16Bytes(maps.heightMap), w, h, 0, 16, b); } });
    jobs.push_back({ "occlusion", "occlusion.png", "linear", 8, "gray",
                     [&](juce::MemoryBlock& b) { return encodePng(grayBytes(maps.occlusion), w, h, 0, 8, b); } });
    jobs.push_back({ "roughness", "roughness.png", "linear", 8, "gray",
                     [&](juce::MemoryBlock& b) { return encodePng(grayBytes(maps.roughness), w, h, 0, 8, b); } });
    jobs.push_back({ "occlusionRoughnessMetallic", "orm.png", "linear", 8, "r=occlusion,g=roughness,b=metallic",
                     [&](juce::MemoryBlock& b) {
                         std::vector<std::uint8_t> orm(static_cast<size_t>(w) * static_cast<size_t>(h) * 3);
                         const auto metal = toByte(maps.metallic);
                         for (size_t i = 0; i < orm.size() / 3; ++i)
                         {
                             orm[i * 3] = toByte(maps.occlusion[i]);
                             orm[i * 3 + 1] = toByte(maps.roughness[i]);
                             orm[i * 3 + 2] = metal;
                         }
                         return encodePng(orm, w, h, 2, 8, b);
                     } });

    if (progress)
        progress(0.0f, "Encoding the maps");
    std::mutex progressLock;
    int done = 0;
    std::vector<std::thread> threads;
    for (auto& job : jobs)
        threads.emplace_back([&job, &progressLock, &done, &progress]() {
            job.ok = job.encode(job.bytes);
            const std::lock_guard<std::mutex> lock(progressLock);
            ++done;
            if (progress)
                progress(static_cast<float>(done) / 6.0f, juce::String("Encoded ") + job.file);
        });
    for (auto& thread : threads)
        thread.join();

    auto* mapsObject = new juce::DynamicObject();
    for (auto& job : jobs)
    {
        if (! job.ok)
        {
            error = juce::String("Could not encode the ") + job.role + " map.";
            return false;
        }
        out.maps.push_back({ folder + job.file, std::move(job.bytes) });
        mapsObject->setProperty(job.role, mapEntry(folder + job.file, job.colourSpace, job.bits, job.channels));
    }

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
