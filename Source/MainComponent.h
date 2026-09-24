#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/interop/ProjectRegistry.h>
#include <creation/services/SuiteAiSettings.h>
#include <creation/services/SuiteVfsJsonStore.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/suite/SuiteStoragePaths.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/ui/SuiteShellController.h>
#include <TexturePluginHost.h>
#include <CreationDock/DockManager.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "NodeGraphPanel.h"

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel
{
public:
    enum class WorkspaceMode
    {
        preview,
        procedural,
        maps,
        adjustments,
        utilities
    };

    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    static constexpr int workspaceModeCount = 5;

    class ViewModeBar final : public juce::Component
    {
    public:
        ViewModeBar();

        std::function<void(WorkspaceMode)> onModeSelected;
        std::function<void()> onPopOutRequested;

        void setActiveMode(WorkspaceMode newMode);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        WorkspaceMode activeMode = WorkspaceMode::preview;
        juce::Label titleLabel;
        juce::TextButton previewButton { "Preview" };
        juce::TextButton proceduralButton { "Procedural" };
        juce::TextButton mapsButton { "Maps" };
        juce::TextButton adjustmentsButton { "Adjustments" };
        juce::TextButton utilitiesButton { "Utilities" };
        juce::TextButton popOutButton { "Pop Out" };
    };

    class WorkspacePanel final : public juce::Component
    {
    public:
        explicit WorkspacePanel(const juce::String& title);

        void setSummaryText(const juce::String& text);
        void resized() override;
        void paint(juce::Graphics& g) override;

    private:
        juce::Label titleLabel;
        juce::TextEditor summaryEditor;
    };

        void configureHeader();
    void configurePanels();
    void loadSuiteState();
    void refreshShellSummary();
    void setActiveMode(WorkspaceMode mode);
    void updateModeButtons();
    void refreshModeVisibility();
    juce::Component* getWorkspaceComponent(WorkspaceMode mode);
    bool isWorkspacePoppedOut(WorkspaceMode mode) const;
    void popOutActiveWorkspace();
    void popOutWorkspace(WorkspaceMode mode, const juce::Rectangle<int>* bounds = nullptr);
    void dockWorkspace(WorkspaceMode mode);
    void saveLayoutState();
    void loadLayoutState();
    juce::ValueTree createLayoutState() const;
    void restoreLayoutState(const juce::ValueTree& state);
    void importPreviewTexture();
    bool importPreviewTextureFromFile(const juce::File& file, bool persistIntoProject);
    bool loadPreviewTextureFromProjectEntry(const juce::String& entryPath);
    void persistProcessedTexture(const juce::Image& image, const juce::String& sourceLabel);
    juce::Image loadTextureImageFromFile(const juce::File& file, juce::String& errorMessage) const;
    juce::Image loadTextureImageFromMemory(const void* data, size_t size, const juce::String& filenameHint, juce::String& errorMessage) const;
    void createNewProject();
    void openProject(const juce::String& projectId);
    void saveProjectState(bool userInitiated = false);
    void loadProjectState();
    bool ensureProjectSessionActive(juce::String& errorMessage);
    void saveAppSettings();
    creation::assets::SuiteAppDomain currentDomain() const noexcept;
    juce::String domainDisplayName() const;
    juce::String registrySummaryText() const;
    juce::String aiSummaryText() const;
    juce::String configSummaryText() const;
    juce::String workbenchSummaryText() const;
    juce::String previewModeText() const;
    juce::String proceduralModeText() const;
    juce::String mapsModeText() const;
    juce::String adjustmentsModeText() const;
    juce::String utilitiesModeText() const;

    CreationSuiteHeaderBar headerBar;
    creation::ui::SuiteShellController suiteShellController;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label runtimeLabel;
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    juce::GroupComponent workspaceGroup;
    juce::GroupComponent resourcesGroup;
    juce::GroupComponent aiGroup;
    juce::GroupComponent configGroup;

    
    NodeGraphPanel nodeGraphPanel;
    ViewerPanel viewerPanel;
    WorkspacePanel mapsWorkspace { "Maps Workspace" };
    WorkspacePanel adjustmentsWorkspace { "Adjustments Workspace" };
    WorkspacePanel utilitiesWorkspace { "Utilities Workspace" };
    juce::Label poppedWorkspacePlaceholder;
    juce::TextEditor resourcesSummary;
    juce::TextEditor aiSummary;
    juce::TextEditor configSummary;

    creation::suite::SuiteSettingsStore suiteSettingsStore;
    creation::services::SuiteAiSettingsStore suiteAiSettingsStore;
    creation::suite::SuiteSettings suiteSettings;
    creation::services::SuiteAiSettings suiteAiSettings;
    creation::assets::ProjectSession projectSession;
    WorkspaceMode activeMode = WorkspaceMode::preview;
    std::array<std::unique_ptr<juce::DocumentWindow>, workspaceModeCount> workspacePopoutWindows;
    std::unique_ptr<juce::FileChooser> previewTextureChooser;

    juce::String lastRegistryError;
    int totalProjectCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

