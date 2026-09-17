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

class MainComponent final : public juce::Component
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

    class PreviewWorkspacePanel final : public juce::Component
    {
    public:
        enum class TextureRole
        {
            baseColor = 0,
            normal,
            roughness,
            metallic,
            emissive,
            mask,
            auxiliary
        };

        struct WorkingTextureItem
        {
            juce::Image image;
            juce::String sourceLabel;
            juce::String projectEntry;
            TextureRole role = TextureRole::baseColor;
            float brightness = 0.0f;
            float contrast = 1.0f;
            float saturation = 1.0f;
            float gamma = 1.0f;
        };

        PreviewWorkspacePanel();
        ~PreviewWorkspacePanel() override;

        void setProjectName(const juce::String& projectName);
        void setStatusText(const juce::String& text);
        void setTextureInfo(const juce::String& text);
        void addWorkingTexture(const juce::Image& image, const juce::String& sourceLabel, const juce::String& projectEntry);
        void clearWorkingTextures();
        void reloadPreview(bool persistDerivedResult = false);
        bool hasTexture() const noexcept;
        juce::String getTextureSourceLabel() const;
        int getWorkingTextureCount() const noexcept;
        juce::String getWorkingSetSummary() const;
        void setSelectedPrimitiveIndex(int index);
        int getSelectedPrimitiveIndex() const noexcept;
        bool isPreviewDirty() const noexcept;
        juce::ValueTree createWorkingSetState() const;
        void restoreWorkingSetState(const juce::ValueTree& state,
                                    const std::function<juce::Image(const juce::String& projectEntry)>& imageLoader);
        void resized() override;

        std::function<void()> onImportTextureRequested;
        std::function<void(const juce::Image&, const juce::String&)> onProcessedTextureReady;

    private:
        class Viewport;

        juce::Label titleLabel;
        juce::Label projectLabel;
        juce::TextButton importTextureButton { "Import Texture" };
        juce::TextButton removeTextureButton { "Remove Selected" };
        juce::TextButton reloadPreviewButton { "Reload Preview" };
        juce::ComboBox primitiveSelector;
        juce::ComboBox workingTextureSelector;
        juce::ComboBox roleSelector;
        juce::Label brightnessLabel;
        juce::Slider brightnessSlider;
        juce::Label contrastLabel;
        juce::Slider contrastSlider;
        juce::Label saturationLabel;
        juce::Slider saturationSlider;
        juce::Label gammaLabel;
        juce::Slider gammaSlider;
        juce::Label statusLabel;
        juce::Label textureInfoLabel;
        std::unique_ptr<Viewport> viewport;
        std::vector<WorkingTextureItem> workingTextures;
        int selectedTextureIndex = -1;
        juce::String activePreviewSourceLabel;

        void refreshWorkingTextureControls();
        static juce::String roleDisplayName(TextureRole role);
        juce::Image applyAdjustments(const WorkingTextureItem& item);
        creation_texture::language::TexturePluginHost texturePluginHost;
        bool previewDirty = false;
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
    ViewModeBar modeBar;

    juce::GroupComponent workspaceGroup;
    juce::GroupComponent resourcesGroup;
    juce::GroupComponent aiGroup;
    juce::GroupComponent configGroup;

    PreviewWorkspacePanel previewWorkspace;
    WorkspacePanel proceduralWorkspace { "Procedural Workspace" };
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

