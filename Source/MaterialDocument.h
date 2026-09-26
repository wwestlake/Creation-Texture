#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>

// The material currently open in the editor: which project asset it is, what it is called, and whether it has
// unsaved edits. Each material is its own named asset in the project (Assets/Source/<slug>.frgraph, kind
// material); saving again under the same name bumps that asset's revision. The graph text itself belongs to
// NodeGraphPanel - this class only moves it in and out of the project.
class MaterialDocument final
{
public:
    explicit MaterialDocument(creation::assets::ProjectSession& session);

    // Every material asset in the open project, newest first. Empty when no project is open.
    juce::Array<creation::assets::AssetDescriptor> listMaterials() const;

    // Starts a new, unnamed material. The caller clears the graph.
    void reset();

    bool open(const creation::assets::AssetDescriptor& asset, juce::String& outGraphText, juce::String& errorMessage);

    // Saves under the current name. Returns false with an empty errorMessage when the material has no name yet -
    // the caller then asks for one and uses saveAs().
    bool save(const juce::String& graphText, juce::String& errorMessage);
    bool saveAs(const juce::String& name, const juce::String& graphText, juce::String& errorMessage);

    bool hasName() const noexcept { return name.isNotEmpty(); }
    juce::String getName() const { return name; }
    juce::String getTitle() const;

    void markEdited() noexcept { edited = true; }
    bool hasUnsavedEdits() const noexcept { return edited; }

private:
    creation::assets::ProjectSession& projectSession;
    juce::String name;
    bool edited = false;
};
