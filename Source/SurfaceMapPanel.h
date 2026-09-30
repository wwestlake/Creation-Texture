#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <SurfaceMaps.h>
#include "ProjectImagePicker.h"
#include "SurfacePreview.h"

// Surface Map mode (Layout > Surface Map): CrazyBump-style maps from one image asset. Pick the source image, set the
// sliders, watch the maps and the lit 3D preview update, then File > Save Surface Map writes a texture set asset.
// Three dockable panels: Settings, Maps, 3D Preview. See docs/REQUIREMENTS.md.
class SurfaceMapWorkspace final : private juce::ChangeListener
{
public:
    SurfaceMapWorkspace();
    ~SurfaceMapWorkspace() override;

    void setProjectSession(creation::assets::ProjectSession* session) { projectSession = session; }
    void setImageSource(project_images::Source source);
    std::function<void(const juce::String&)> onStatus;

    juce::Component& getSettingsPanel() noexcept;
    juce::Component& getMapsPanel() noexcept;
    juce::Component& getPreview() noexcept { return preview; }

    bool canSave() const noexcept;
    void saveSurfaceMap();
    // Reopens a saved texture set exactly as it was saved: its source image and every setting.
    bool canOpen() const noexcept { return projectSession != nullptr && projectSession->isValid(); }
    void openSurfaceMap();

    // Used by the panels.
    const surface_maps::Settings& getSettings() const noexcept { return settings; }
    void setSettings(const surface_maps::Settings& newSettings);
    void setSource(const juce::String& logicalPath);
    juce::String getSourcePath() const { return sourcePath; }
    const project_images::Source& getImageSource() const noexcept { return images; }

private:
    class Worker;
    class SettingsPanel;
    class MapsPanel;
    class SaveJob;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void requestMaps();
    void status(const juce::String& text);

    creation::assets::ProjectSession* projectSession = nullptr;
    project_images::Source images;
    surface_maps::Settings settings;

    juce::String sourcePath, sourceName;
    juce::String openSetName;             // the texture set being edited, if one was opened or saved
    juce::Image sourceImage;              // full size, for saving
    std::vector<float> previewPixels;     // linear rgba, at most 1024 px on a side
    int previewWidth = 0, previewHeight = 0;

    surface_maps::Maps maps;              // latest preview maps
    juce::String mapsError;

    std::unique_ptr<Worker> worker;
    std::unique_ptr<SettingsPanel> settingsPanel;
    std::unique_ptr<MapsPanel> mapsPanel;
    SurfacePreview preview;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SurfaceMapWorkspace)
};
