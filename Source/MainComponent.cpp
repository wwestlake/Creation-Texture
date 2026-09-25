#include "MainComponent.h"
#include "Branding.h"
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/ProjectContainerService.h>
#include <creation/assets/ProjectAssetService.h>

MainComponent::MainComponent()
{
    headerBar.setAppTitle("Djehuti Texture");
    headerBar.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
    headerBar.setProjectLabel("Project: No active texture project");
    headerBar.setTransportControlsVisible(false); headerBar.audioButton.setVisible(false); 
    
    suiteShellController.onProjectOpenRequested = [this](const juce::String& projectId) {
        confirmDiscardingEdits([this, projectId]() { openProject(projectId); });
    };

    creation::ui::SuiteAssetManagerCapability assetCapability;
    assetCapability.hostAppDisplayName = "Djehuti Texture";
    assetCapability.appDomain = creation::assets::SuiteAppDomain::texture;
    assetCapability.enumerateProjectAssets = [this]() {
        if (projectSession.isValid()) {
            return projectSession.getManifest().assetCatalog.assets;
        }
        return juce::Array<creation::assets::AssetDescriptor>();
    };
    
    suiteShellController.attach(headerBar,
                                {
                                    "Djehuti Texture",
                                    creation::assets::SuiteAppDomain::texture,
                                    creation_texture::branding::backgroundColour(),
                                    assetCapability
                                },
                                [this](const juce::String& status)
                                {
                                    headerBar.setStatusText(status);
                                });
                                
    addAndMakeVisible(headerBar);
    
    menuBar = std::make_unique<juce::MenuBarComponent>(this);
    addAndMakeVisible(menuBar.get());

    dockManager = std::make_unique<CreationDock::DockManager>(*this);
    addAndMakeVisible(dockManager.get());
    
    // Register dock panels
    dockManager->registerPanel("NodeGraph", "Node Graph", std::make_unique<NonOwningPanelHost>(nodeGraphPanel), CreationDock::DockTargetZone::CenterTab);
    dockManager->registerPanel("Viewer", "3D Preview", std::make_unique<NonOwningPanelHost>(viewerPanel), CreationDock::DockTargetZone::Right);

    setSize(1600, 1000);

    nodeGraphPanel.setProjectSession(&projectSession);
    nodeGraphPanel.onSaveRequested = [this]() { saveMaterial(); };
    nodeGraphPanel.onGraphEdited = [this]() {
        materialDocument.markEdited();
        refreshTitle();
    };

    juce::String error;
    ensureProjectSessionActive(error);
    nodeGraphPanel.addViewer(viewerPanel.getViewer());
    refreshTitle();
}

MainComponent::~MainComponent()
{
    menuBar.reset();
    dockManager.reset();
}

void MainComponent::openProject(const juce::String& projectId)
{
    juce::String error;
    creation::suite::SuiteSettingsStore store;
    if (!creation::assets::ProjectWorkspaceService::openProject(store.load(error), projectId, projectSession, error))
    {
        headerBar.setStatusText("Failed to open project: " + error);
        return;
    }
    
    materialDocument.reset();
    nodeGraphPanel.clearGraph();
    refreshTitle();
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(creation_texture::branding::backgroundColour());
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    headerBar.setBounds(bounds.removeFromTop(96));
    menuBar->setBounds(bounds.removeFromTop(juce::LookAndFeel::getDefaultLookAndFeel().getDefaultMenuBarHeight()));
    dockManager->setBounds(bounds);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName)
{
    juce::PopupMenu menu;
    if (menuName == "File")
    {
        menu.addItem(1, "New Material");
        menu.addItem(2, "Open Material...", projectSession.isValid());
        menu.addSeparator();
        menu.addItem(3, "Save Material", projectSession.isValid());
        menu.addItem(4, "Save Material As...", projectSession.isValid());
    }
    else if (menuName == "Edit")
    {
        menu.addItem(10, "Undo");
        menu.addItem(11, "Redo");
        menu.addSeparator();
        menu.addItem(12, "Cut");
        menu.addItem(13, "Copy");
        menu.addItem(14, "Paste");
        menu.addItem(15, "Delete");
    }
    else if (menuName == "View")
    {
        menu.addItem(20, "Virtual Engineer");
        menu.addItem(21, "Node Graph");
        menu.addItem(22, "3D Preview");
    }
    else if (menuName == "Help")
    {
        menu.addItem(30, "Documentation");
        menu.addItem(31, "About Djehuti Texture");
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    if (menuItemID == 1)
        confirmDiscardingEdits([this]() { newMaterial(); });
    else if (menuItemID == 2)
        confirmDiscardingEdits([this]() { showOpenMaterialMenu(); });
    else if (menuItemID == 3)
        saveMaterial();
    else if (menuItemID == 4)
        saveMaterialAs();
    else if (menuItemID >= 10 && menuItemID <= 15)
    {
        headerBar.setStatusText("Edit action to be implemented");
    }
    else if (menuItemID == 20)
    {
        dockManager->activatePanel("Frusty");
    }
    else if (menuItemID == 21)
    {
        dockManager->activatePanel("NodeGraph");
    }
    else if (menuItemID == 22)
    {
        dockManager->activatePanel("Viewer");
    }
    else if (menuItemID >= 30 && menuItemID <= 31)
    {
        headerBar.setStatusText("Help action to be implemented");
    }
}



bool MainComponent::ensureProjectSessionActive(juce::String& errorMessage)
{
    if (projectSession.isValid())
        return true;

    creation::suite::SuiteSettingsStore store;
    auto settings = store.load(errorMessage);
    if (errorMessage.isNotEmpty()) return false;

    auto projects = creation::assets::ProjectContainerService::listProjects(settings, errorMessage);
    if (projects.isEmpty()) {
        errorMessage = "No project open. Please open or create a project first.";
        headerBar.setStatusText(errorMessage);
        return false;
    }

    int latestIdx = 0;
    for (int i = 1; i < projects.size(); ++i) {
        if (projects.getReference(i).manifest.modifiedAt > projects.getReference(latestIdx).manifest.modifiedAt) {
            latestIdx = i;
        }
    }
    if (creation::assets::ProjectWorkspaceService::openProject(settings, projects.getReference(latestIdx).projectId, projectSession, errorMessage)) {
        refreshTitle();
        return true;
    }
    return false;
}

void MainComponent::newMaterial()
{
    materialDocument.reset();
    nodeGraphPanel.clearGraph();
    refreshTitle();
}

void MainComponent::showOpenMaterialMenu()
{
    const auto materials = materialDocument.listMaterials();

    juce::PopupMenu menu;
    menu.addSectionHeader("Materials in this project");
    if (materials.isEmpty())
        menu.addItem(1, "No saved materials yet", false);
    for (int i = 0; i < materials.size(); ++i)
        menu.addItem(100 + i, materials.getReference(i).displayName);

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(menuBar.get()),
                       [this, materials](int result) {
                           const int index = result - 100;
                           if (index >= 0 && index < materials.size())
                               openMaterial(materials.getReference(index));
                       });
}

void MainComponent::openMaterial(const creation::assets::AssetDescriptor& asset)
{
    juce::String graphText, error;
    if (! materialDocument.open(asset, graphText, error) || ! nodeGraphPanel.loadGraphText(graphText, error))
    {
        headerBar.setStatusText("Could not open material: " + error);
        materialDocument.reset();
        refreshTitle();
        return;
    }

    headerBar.setStatusText("Opened " + asset.displayName + ".");
    refreshTitle();
}

void MainComponent::saveMaterial()
{
    if (! materialDocument.hasName())
    {
        saveMaterialAs();
        return;
    }

    juce::String error;
    if (materialDocument.save(nodeGraphPanel.getGraphText(), error))
        headerBar.setStatusText("Saved " + materialDocument.getName() + ".");
    else
        headerBar.setStatusText("Could not save material: " + error);
    refreshTitle();
}

void MainComponent::saveMaterialAs()
{
    if (! projectSession.isValid())
    {
        headerBar.setStatusText("No project is open. Open or create a project first.");
        return;
    }

    auto* prompt = new juce::AlertWindow("Save Material", "Name this material:", juce::MessageBoxIconType::NoIcon, this);
    prompt->addTextEditor("name", materialDocument.getName());
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt](int result) {
        if (result != 1)
            return;

        juce::String error;
        const auto name = prompt->getTextEditorContents("name");
        if (materialDocument.saveAs(name, nodeGraphPanel.getGraphText(), error))
            headerBar.setStatusText("Saved " + materialDocument.getName() + ".");
        else
            headerBar.setStatusText("Could not save material: " + error);
        refreshTitle();
    }), true);
}

void MainComponent::confirmDiscardingEdits(std::function<void()> proceed)
{
    if (! materialDocument.hasUnsavedEdits())
    {
        proceed();
        return;
    }

    const auto materialName = materialDocument.hasName() ? materialDocument.getName() : juce::String("This material");
    juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::QuestionIcon,
                                       "Unsaved changes",
                                       materialName + " has changes that are not saved. Discard them?",
                                       "Discard", "Cancel", this,
                                       juce::ModalCallbackFunction::create([proceed](int result) {
                                           if (result == 1)
                                               proceed();
                                       }));
}

void MainComponent::refreshTitle()
{
    const auto projectName = projectSession.isValid() ? projectSession.getManifest().projectName
                                                      : juce::String("No project open");
    headerBar.setProjectLabel("Project: " + projectName + "  |  " + materialDocument.getTitle());
}
