#include "ProjectImages.h"

void ProjectImages::setProjectSession(creation::assets::ProjectSession* session)
{
    projectSession = session;
    clear();
}

void ProjectImages::clear()
{
    images.clear();
    thumbnails.clear();
}

bool ProjectImages::isImageAsset(const creation::assets::AssetDescriptor& asset)
{
    // Only formats this app can decode. Recognised by what the file is, not by an asset-kind label.
    static const juce::StringArray imageExtensions { "png", "jpg", "jpeg", "gif" };
    const auto extension = asset.logicalPath.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return imageExtensions.contains(extension);
}

project_images::Source ProjectImages::source()
{
    project_images::Source source;
    source.list = [this]() {
        juce::Array<project_images::Entry> entries;
        if (projectSession == nullptr || ! projectSession->isValid())
            return entries;
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (isImageAsset(asset))
                entries.add({ asset.displayName.isNotEmpty() ? asset.displayName : asset.logicalPath.fromLastOccurrenceOf("/", false, false),
                              asset.logicalPath });
        return entries;
    };
    source.thumbnail = [this](const juce::String& logicalPath) { return thumbnail(logicalPath); };
    source.image = [this](const juce::String& logicalPath) { return image(logicalPath); };
    return source;
}

juce::Image ProjectImages::image(const juce::String& logicalPath)
{
    const auto key = logicalPath.toStdString();
    auto cached = images.find(key);
    if (cached != images.end())
        return cached->second;

    juce::Image result;
    juce::MemoryBlock block;
    if (projectSession != nullptr && projectSession->isValid() && projectSession->readEntry(logicalPath, block))
        result = juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());
    images[key] = result;
    return result;
}

juce::Image ProjectImages::thumbnail(const juce::String& logicalPath)
{
    const auto key = logicalPath.toStdString();
    auto cached = thumbnails.find(key);
    if (cached != thumbnails.end())
        return cached->second;

    juce::Image result;
    const auto full = image(logicalPath);
    if (full.isValid())
    {
        const float scale = juce::jmin(1.0f, 96.0f / static_cast<float>(juce::jmax(full.getWidth(), full.getHeight())));
        result = full.rescaled(juce::jmax(1, juce::roundToInt(static_cast<float>(full.getWidth()) * scale)),
                               juce::jmax(1, juce::roundToInt(static_cast<float>(full.getHeight()) * scale)),
                               juce::Graphics::mediumResamplingQuality);
        // The slot shows the real size, not the thumbnail's.
        result.getProperties()->set("sourceWidth", full.getWidth());
        result.getProperties()->set("sourceHeight", full.getHeight());
    }
    thumbnails[key] = result;
    return result;
}
