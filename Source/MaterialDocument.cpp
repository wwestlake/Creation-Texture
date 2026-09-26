#include "MaterialDocument.h"

#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>

namespace
{
juce::String slugForMaterialName(const juce::String& materialName)
{
    auto slug = materialName.trim().toLowerCase();
    slug = slug.retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-_ ");
    slug = slug.replace(" ", "-");
    while (slug.contains("--"))
        slug = slug.replace("--", "-");
    slug = slug.trimCharactersAtStart("-").trimCharactersAtEnd("-");
    return slug.isNotEmpty() ? slug : "material";
}
}

MaterialDocument::MaterialDocument(creation::assets::ProjectSession& session)
    : projectSession(session)
{
}

juce::Array<creation::assets::AssetDescriptor> MaterialDocument::listMaterials() const
{
    if (! projectSession.isValid())
        return {};

    auto materials = projectSession.getManifest().assetCatalog.query({ creation::assets::AssetKind::material });
    std::sort(materials.begin(), materials.end(), [](const auto& a, const auto& b) { return a.modifiedAt > b.modifiedAt; });
    return materials;
}

void MaterialDocument::reset()
{
    name.clear();
    edited = false;
}

bool MaterialDocument::open(const creation::assets::AssetDescriptor& asset, juce::String& outGraphText, juce::String& errorMessage)
{
    if (! projectSession.isValid())
    {
        errorMessage = "No project is open.";
        return false;
    }

    juce::MemoryBlock data;
    if (! projectSession.readEntry(asset.logicalPath, data))
    {
        errorMessage = "Could not read " + asset.displayName + " from the project.";
        return false;
    }

    outGraphText = data.toString();
    name = asset.displayName;
    edited = false;
    return true;
}

bool MaterialDocument::save(const juce::String& graphText, juce::String& errorMessage)
{
    if (! hasName())
        return false;

    return saveAs(name, graphText, errorMessage);
}

bool MaterialDocument::saveAs(const juce::String& newName, const juce::String& graphText, juce::String& errorMessage)
{
    const auto trimmedName = newName.trim();
    if (trimmedName.isEmpty())
    {
        errorMessage = "A material needs a name.";
        return false;
    }

    if (! projectSession.isValid())
    {
        errorMessage = "No project is open. Open or create a project first.";
        return false;
    }

    juce::MemoryBlock data(graphText.toRawUTF8(), graphText.getNumBytesAsUTF8());

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::material;
    options.displayName = trimmedName;
    options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot)
                        + slugForMaterialName(trimmedName) + ".frgraph";
    options.mediaType = "application/x-creation-node-graph";
    options.sourceApp = "Djehuti Texture";
    options.sourceTool = "Material Graph";
    options.description = "Material node graph.";

    creation::assets::AssetDescriptor saved;
    if (! creation::assets::ProjectAssetService::saveGeneratedAsset(projectSession, data, options, saved, errorMessage))
        return false;

    if (! projectSession.commit(errorMessage))
        return false;

    name = trimmedName;
    edited = false;
    return true;
}

juce::String MaterialDocument::getTitle() const
{
    return (hasName() ? name : juce::String("Untitled material")) + (edited ? " *" : "");
}
