#pragma once
#include <JuceHeader.h>
#include <TexturePluginHost.h>

class ViewerPanel final : public juce::Component
{
public:
    enum class TextureRole { baseColor = 0, normal, roughness, metallic, emissive, mask, auxiliary };
    struct WorkingTextureItem {
        juce::Image image; juce::String sourceLabel; juce::String projectEntry;
        TextureRole role = TextureRole::baseColor;
        float brightness = 0.0f; float contrast = 1.0f; float saturation = 1.0f; float gamma = 1.0f;
    };

    ViewerPanel();
    ~ViewerPanel() override;

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
    void restoreWorkingSetState(const juce::ValueTree& state, const std::function<juce::Image(const juce::String&)>& imageLoader);
    void resized() override;

    std::function<void()> onImportTextureRequested;
    std::function<void(const juce::Image&, const juce::String&)> onProcessedTextureReady;

private:
    class Viewport;
    juce::Label titleLabel; juce::Label projectLabel; juce::TextButton importTextureButton { "Import Texture" };
    juce::TextButton removeTextureButton { "Remove Selected" }; juce::TextButton reloadPreviewButton { "Reload Preview" };
    juce::ComboBox primitiveSelector; juce::ComboBox workingTextureSelector; juce::ComboBox roleSelector;
    juce::Label brightnessLabel; juce::Slider brightnessSlider; juce::Label contrastLabel; juce::Slider contrastSlider;
    juce::Label saturationLabel; juce::Slider saturationSlider; juce::Label gammaLabel; juce::Slider gammaSlider;
    juce::Label statusLabel; juce::Label textureInfoLabel;
    std::unique_ptr<Viewport> viewport;
    std::vector<WorkingTextureItem> workingTextures;
    int selectedTextureIndex = -1; juce::String activePreviewSourceLabel;

    void refreshWorkingTextureControls();
    static juce::String roleDisplayName(TextureRole role);
    juce::Image applyAdjustments(const WorkingTextureItem& item);
    creation_texture::language::TexturePluginHost texturePluginHost;
    bool previewDirty = false;
};
