const fs = require('fs');

let mainH = fs.readFileSync('apps/CreationTexture/Source/MainComponent.h', 'utf8');

// Add include
if (!mainH.includes('<CreationDock/DockManager.h>')) {
    mainH = mainH.replace('#include <TexturePluginHost.h>', '#include <TexturePluginHost.h>\n#include <CreationDock/DockManager.h>\n#include <juce_gui_extra/juce_gui_extra.h>');
}

// Add inheritance
if (!mainH.includes('private juce::MenuBarModel')) {
    mainH = mainH.replace('class MainComponent final : public juce::Component', 'class MainComponent final : public juce::Component,\n                            private juce::MenuBarModel');
}

// Add MenuBarModel overrides
if (!mainH.includes('getMenuBarNames')) {
    mainH = mainH.replace('void resized() override;', 'void resized() override;\n    juce::StringArray getMenuBarNames() override;\n    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;\n    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;');
}

// Remove ViewModeBar
mainH = mainH.replace(/class ViewModeBar final : public juce::Component[\s\S]*?    };\n/g, '');
mainH = mainH.replace('ViewModeBar modeBar;', 'std::unique_ptr<juce::MenuBarComponent> menuBar;\n    std::unique_ptr<CreationDock::DockManager> dockManager;');
mainH = mainH.replace('void updateModeButtons();\n', '');

fs.writeFileSync('apps/CreationTexture/Source/MainComponent.h', mainH);

let mainC = fs.readFileSync('apps/CreationTexture/Source/MainComponent.cpp', 'utf8');

// Add nonowningpanelhost to cpp
if (!mainC.includes('class NonOwningPanelHost')) {
    mainC = mainC.replace('namespace\n{\n', 'namespace\n{\nclass NonOwningPanelHost final : public juce::Component\n{\npublic:\n    explicit NonOwningPanelHost(juce::Component& contentToHost) : content(contentToHost)\n    {\n        addAndMakeVisible(content);\n    }\n    void resized() override\n    {\n        content.setBounds(getLocalBounds());\n    }\nprivate:\n    juce::Component& content;\n};\n');
}

mainC = mainC.replace(/void MainComponent::configurePanels\(\)[\s\S]*?void MainComponent::updateModeButtons\(\)/g, 'void MainComponent::configurePanels()\n{\n}\nvoid MainComponent::updateModeButtons()\n{\n}');

mainC = mainC.replace(/MainComponent::MainComponent\(\)\n\{[\s\S]*?loadSuiteState\(\);\n\}/, `MainComponent::MainComponent()
{
    configureHeader();
    
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());
    
    dockManager = std::make_unique<CreationDock::DockManager>(true);
    addAndMakeVisible(dockManager.get());
    
    dockManager->registerPanel("Node Graph", [this]() {
        return new NonOwningPanelHost(nodeGraphPanel);
    });
    
    dockManager->registerPanel("Viewer", [this]() {
        return new NonOwningPanelHost(viewerPanel);
    });
    
    dockManager->registerPanel("Maps", [this]() {
        return new NonOwningPanelHost(mapsWorkspace);
    });
    
    dockManager->registerPanel("Adjustments", [this]() {
        return new NonOwningPanelHost(adjustmentsWorkspace);
    });
    
    dockManager->registerPanel("Utilities", [this]() {
        return new NonOwningPanelHost(utilitiesWorkspace);
    });
    
    dockManager->dockPanel("Node Graph", CreationDock::DockTargetZone::center);
    dockManager->dockPanel("Viewer", CreationDock::DockTargetZone::right);
    
    setSize(1280, 800);
    
    loadSuiteState();
}`);

mainC = mainC.replace(/void MainComponent::resized\(\)[\s\S]*?void MainComponent::refreshShellSummary\(\)/, `void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    headerBar.setBounds(bounds.removeFromTop(44));
    menuBar->setBounds(bounds.removeFromTop(24));
    dockManager->setBounds(bounds);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File")
    {
        menu.addItem(1, "Open Material...");
    }
    else if (menuName == "View")
    {
        menu.addItem(10, "Toggle Node Graph", true, true);
        menu.addItem(11, "Toggle Viewer", true, true);
        menu.addItem(12, "Toggle Maps", true, true);
        menu.addItem(13, "Toggle Adjustments", true, true);
        menu.addItem(14, "Toggle Utilities", true, true);
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    if (menuItemID == 1) {
        importPreviewTexture();
    } else if (menuItemID >= 10 && menuItemID <= 14) {
        juce::String panelName;
        if (menuItemID == 10) panelName = "Node Graph";
        else if (menuItemID == 11) panelName = "Viewer";
        else if (menuItemID == 12) panelName = "Maps";
        else if (menuItemID == 13) panelName = "Adjustments";
        else if (menuItemID == 14) panelName = "Utilities";
        
        dockManager->togglePanel(panelName, CreationDock::DockTargetZone::center);
    }
}

void MainComponent::refreshShellSummary()`);

// Clean up some mode bar usages that might fail to compile
mainC = mainC.replace(/MainComponent::ViewModeBar::[\s\S]*?MainComponent::WorkspacePanel::/g, 'MainComponent::WorkspacePanel::');

fs.writeFileSync('apps/CreationTexture/Source/MainComponent.cpp', mainC);
console.log('done rewriting');
