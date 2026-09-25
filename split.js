const fs = require('fs');

let cmake = fs.readFileSync('apps/CreationTexture/CMakeLists.txt', 'utf8');
cmake = cmake.replace('Source/ProceduralWorkspacePanel.cpp', 'Source/NodeGraphPanel.cpp\n    Source/ViewerPanel.cpp');
cmake = cmake.replace('Source/ProceduralWorkspacePanel.h', 'Source/NodeGraphPanel.h\n    Source/ViewerPanel.h');
fs.writeFileSync('apps/CreationTexture/CMakeLists.txt', cmake);

// NodeGraphPanel
let procH = fs.readFileSync('apps/CreationTexture/Source/ProceduralWorkspacePanel.h', 'utf8');
procH = procH.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
fs.writeFileSync('apps/CreationTexture/Source/NodeGraphPanel.h', procH);

let procC = fs.readFileSync('apps/CreationTexture/Source/ProceduralWorkspacePanel.cpp', 'utf8');
procC = procC.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
procC = procC.replace(/ProceduralWorkspacePanel\.h/g, 'NodeGraphPanel.h');
fs.writeFileSync('apps/CreationTexture/Source/NodeGraphPanel.cpp', procC);

// Delete the old ProceduralWorkspacePanel files
try { fs.unlinkSync('apps/CreationTexture/Source/ProceduralWorkspacePanel.h'); } catch(e){}
try { fs.unlinkSync('apps/CreationTexture/Source/ProceduralWorkspacePanel.cpp'); } catch(e){}

let mainH = fs.readFileSync('apps/CreationTexture/Source/MainComponent.h', 'utf8');
let mainC = fs.readFileSync('apps/CreationTexture/Source/MainComponent.cpp', 'utf8');

// For simplicity, we will just declare ViewerPanel as a separate file, and copy the whole PreviewWorkspacePanel implementation.
let viewerH = `#pragma once
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
`;
fs.writeFileSync('apps/CreationTexture/Source/ViewerPanel.h', viewerH);

let previewImplRegex = /class MainComponent::PreviewWorkspacePanel::Viewport.*?MainComponent::PreviewWorkspacePanel::restoreWorkingSetState[^\n]*\n\{[\s\S]*?\n\}/g;
let match = previewImplRegex.exec(mainC);
let viewerC = `#include "ViewerPanel.h"\n#include <gl/GL.h>\n\n`;
if (match) {
    let impl = match[0];
    impl = impl.replace(/MainComponent::PreviewWorkspacePanel/g, 'ViewerPanel');
    viewerC += impl;
}
fs.writeFileSync('apps/CreationTexture/Source/ViewerPanel.cpp', viewerC);

mainH = mainH.replace(/class PreviewWorkspacePanel final : public juce::Component[\s\S]*?    };\n/g, '');
mainC = mainC.replace(previewImplRegex, '');

mainH = mainH.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
mainH = mainH.replace(/PreviewWorkspacePanel/g, 'ViewerPanel');
mainH = mainH.replace(/NodeGraphPanel proceduralWorkspace;/g, 'NodeGraphPanel nodeGraphPanel;\n    ViewerPanel viewerPanel;');
mainH = mainH.replace(/ViewerPanel previewWorkspace;/g, '');

mainC = mainC.replace(/proceduralWorkspace/g, 'nodeGraphPanel');
mainC = mainC.replace(/previewWorkspace/g, 'viewerPanel');
mainC = mainC.replace(/ProceduralWorkspacePanel/g, 'NodeGraphPanel');
mainC = mainC.replace(/PreviewWorkspacePanel/g, 'ViewerPanel');
mainC = mainC.replace(/#include "NodeGraphPanel\.h"/g, '#include "NodeGraphPanel.h"\n#include "ViewerPanel.h"');

fs.writeFileSync('apps/CreationTexture/Source/MainComponent.h', mainH);
fs.writeFileSync('apps/CreationTexture/Source/MainComponent.cpp', mainC);
console.log('done splitting panels');
