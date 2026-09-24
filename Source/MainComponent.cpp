#include "MainComponent.h"
#include "Branding.h"
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/ProjectContainerService.h>

MainComponent::MainComponent()
{
    headerBar.setAppTitle("Djehuti Texture");
    headerBar.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
    headerBar.setProjectLabel("Project: No active texture project");
    headerBar.setTransportControlsVisible(false); headerBar.audioButton.setVisible(false); 
    
    suiteShellController.onProjectOpenRequested = [this](const juce::String& projectId) {
        openProject(projectId);
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
    nodeGraphPanel.onEnsureProjectSessionActive = [this](juce::String& err) { return ensureProjectSessionActive(err); };

    juce::String error;
    ensureProjectSessionActive(error);
    nodeGraphPanel.addViewer(viewerPanel.getViewer());
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
    
    headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
    
    juce::MemoryBlock block;
    if (projectSession.readEntry("material.frgraph", block))
    {
        // For now, save out to temp file and load since loadGraph takes File
        auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("material.frgraph");
        tempFile.replaceWithData(block.getData(), block.getSize());
        nodeGraphPanel.loadGraph(tempFile);
    }
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
        menu.addItem(1, "Save");
        menu.addSeparator();
        menu.addItem(2, "Import Texture...");
    }
    else if (menuName == "View")
    {
        menu.addItem(4, "Node Graph");
        menu.addItem(5, "3D Preview");
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex)
{
    if (menuItemID == 1) // Save
    {
        if (projectSession.isValid())
        {
            auto tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("material.frgraph");
            nodeGraphPanel.saveGraph(tempFile);
            
            juce::MemoryBlock block;
            tempFile.loadFileAsData(block);
            projectSession.writeEntry("material.frgraph", block);
            
            juce::String err;
            if (projectSession.commit(err))
                headerBar.setStatusText("Material saved to project.");
            else
                headerBar.setStatusText("Failed to save material: " + err);
        }
        else
        {
            headerBar.setStatusText("No project is open. Cannot save.");
        }
    }
    else if (menuItemID == 2) // Import Texture
    {
        headerBar.setStatusText("Import Texture workflow to be implemented.");
    }
    else if (menuItemID == 4)
    {
        dockManager->activatePanel("NodeGraph");
    }
    else if (menuItemID == 5)
    {
        dockManager->activatePanel("Viewer");
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
        headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
        return true;
    }
    return false;
}



