#include "MainComponent.h"
#include "Branding.h"
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/ProjectContainerService.h>
#include <creation/assets/ProjectAssetService.h>
#include <creation/services/SuiteVfsJsonStore.h>
#include <creation/ui/SuiteJUCEApplication.h>

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

    // Work-area switcher, under the header: each area swaps in its own whole layout.
    for (auto* tab : { &materialsTab, &imageLabTab })
    {
        tab->setClickingTogglesState(false);
        tab->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2f5d8a));
        addAndMakeVisible(*tab);
    }
    materialsTab.setConnectedEdges(juce::Button::ConnectedOnRight);
    imageLabTab.setConnectedEdges(juce::Button::ConnectedOnLeft);
    materialsTab.onClick = [this]() { showWorkArea(WorkArea::materials); };
    imageLabTab.onClick = [this]() { showWorkArea(WorkArea::imageLab); };

    using Zone = CreationDock::DockTargetZone;
    materialsDock = std::make_unique<CreationDock::DockManager>(*this);
    materialsDock->registerPanel("Nodes", "Nodes", std::make_unique<NonOwningPanelHost>(nodeGraphPanel.getPalette()), Zone::Left);
    materialsDock->registerPanel("NodeGraph", "Node Graph", std::make_unique<NonOwningPanelHost>(nodeGraphPanel), Zone::CenterTab);
    materialsDock->registerPanel("Viewer", "3D Preview", std::make_unique<NonOwningPanelHost>(viewerPanel), Zone::Right);
    materialsDock->registerPanel("Properties", "Properties", std::make_unique<NonOwningPanelHost>(propertiesPanel), Zone::Right);
    materialsDock->activatePanel("Viewer");
    addChildComponent(materialsDock.get());

    imageLabDock = std::make_unique<CreationDock::DockManager>(*this);
    imageLabDock->registerPanel("Canvas", "Canvas", std::make_unique<NonOwningPanelHost>(imageLab.getCanvas()), Zone::CenterTab);
    imageLabDock->registerPanel("Layers", "Layers", std::make_unique<NonOwningPanelHost>(imageLab.getLayersPanel()), Zone::Right);
    imageLabDock->registerPanel("History", "History", std::make_unique<NonOwningPanelHost>(imageLab.getHistoryPanel()), Zone::Right);
    imageLabDock->activatePanel("Layers");
    addChildComponent(imageLabDock.get());

    loadLayouts();
    showWorkArea(currentArea);

    setSize(1600, 1000);

    nodeGraphPanel.setProjectSession(&projectSession);
    nodeGraphPanel.onSaveRequested = [this]() { saveMaterial(); };

    NodePropertiesPanel::Host propertiesHost;
    propertiesHost.graph = &nodeGraphPanel.getGraph();
    propertiesHost.registry = &nodeGraphPanel.getRegistry();
    propertiesHost.projectImages = nodeGraphPanel.projectImageSource();
    propertiesHost.onValueEdited = [this]() { nodeGraphPanel.applyPropertyEdit(); };
    propertiesPanel.setHost(std::move(propertiesHost));

    imageLab.setProjectSession(&projectSession);
    imageLab.setImageSource(nodeGraphPanel.projectImageSource());
    imageLab.onStatus = [this](const juce::String& text) { headerBar.setStatusText(text); };
    nodeGraphPanel.onSelectionChanged = [this](ce::node_system::NodeId id) { propertiesPanel.showNode(id); };
    nodeGraphPanel.onGraphStructureChanged = [this]() { propertiesPanel.refresh(); };
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
    saveLayouts();
    menuBar.reset();
    materialsDock.reset();
    imageLabDock.reset();
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
    auto tabs = bounds.removeFromTop(34).reduced(8, 4);
    materialsTab.setBounds(tabs.removeFromLeft(120));
    imageLabTab.setBounds(tabs.removeFromLeft(120));
    materialsDock->setBounds(bounds);
    imageLabDock->setBounds(bounds);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    if (currentArea == WorkArea::imageLab)
        return { "File", "Edit", "Layer", "View", "Help" };
    return { "File", "View", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu menu;
    const bool project = projectSession.isValid();
    const bool imageLabArea = currentArea == WorkArea::imageLab;
    auto& layers = imageLab.getDocument();

    if (menuName == "File" && ! imageLabArea)
    {
        menu.addItem(1, "New Material");
        menu.addItem(2, "Open Material...", project);
        menu.addSeparator();
        menu.addItem(3, "Save Material", project);
        menu.addItem(4, "Save Material As...", project);
    }
    else if (menuName == "File")
    {
        menu.addItem(5, "Save Image...", project && layers.getNumLayers() > 0);
    }
    else if (menuName == "Edit")
    {
        auto& undo = layers.getUndoManager();
        menu.addItem(10, "Undo " + undo.getUndoDescription(), undo.canUndo());
        menu.addItem(11, "Redo " + (undo.getRedoDescriptions().isEmpty() ? juce::String() : undo.getRedoDescriptions()[0]), undo.canRedo());
    }
    else if (menuName == "Layer")
    {
        const bool hasLayer = layers.getLayer(layers.getActiveIndex()) != nullptr;
        menu.addItem(40, "Add Image Layer...", project);
        menu.addItem(41, "Duplicate Layer", hasLayer);
        menu.addItem(42, "Delete Layer", hasLayer);
    }
    else if (menuName == "View")
    {
        if (imageLabArea)
        {
            menu.addItem(25, "Canvas");
            menu.addItem(26, "Layers");
            menu.addItem(27, "History");
        }
        else
        {
            menu.addItem(20, "Nodes");
            menu.addItem(21, "Node Graph");
            menu.addItem(22, "3D Preview");
            menu.addItem(23, "Properties");
        }
        menu.addSeparator();
        menu.addItem(29, "Reset Layout");
    }
    else if (menuName == "Help")
    {
        menu.addItem(31, "About Djehuti Texture");
    }
    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int)
{
    auto& dock = dockFor(currentArea);
    switch (menuItemID)
    {
        case 1: confirmDiscardingEdits([this]() { newMaterial(); }); break;
        case 2: confirmDiscardingEdits([this]() { showOpenMaterialMenu(); }); break;
        case 3: saveMaterial(); break;
        case 4: saveMaterialAs(); break;
        case 5: imageLab.saveImage(); break;
        case 10: imageLab.undo(); break;
        case 11: imageLab.redo(); break;
        case 20: dock.activatePanel("Nodes"); break;
        case 21: dock.activatePanel("NodeGraph"); break;
        case 22: dock.activatePanel("Viewer"); break;
        case 23: dock.activatePanel("Properties"); break;
        case 25: dock.activatePanel("Canvas"); break;
        case 26: dock.activatePanel("Layers"); break;
        case 27: dock.activatePanel("History"); break;
        case 29: dock.resetLayout(); saveLayouts(); break;
        case 31:
            if (auto* app = dynamic_cast<creation::ui::SuiteJUCEApplication*>(juce::JUCEApplication::getInstance()))
                app->showAboutBox();
            break;
        case 40: imageLab.addImageLayer(); break;
        case 41: imageLab.getDocument().duplicateActive(); break;
        case 42: imageLab.getDocument().removeActive(); break;
        default: break;
    }
}

CreationDock::DockManager& MainComponent::dockFor(WorkArea area)
{
    return area == WorkArea::imageLab ? *imageLabDock : *materialsDock;
}

void MainComponent::showWorkArea(WorkArea area)
{
    currentArea = area;
    materialsDock->setVisible(area == WorkArea::materials);
    imageLabDock->setVisible(area == WorkArea::imageLab);
    materialsTab.setToggleState(area == WorkArea::materials, juce::dontSendNotification);
    imageLabTab.setToggleState(area == WorkArea::imageLab, juce::dontSendNotification);
    menuItemsChanged();
    saveLayouts();
}

namespace
{
constexpr const char* layoutStorePath = "texture-layout.json";
}

// Each work area's arrangement, and which one was open, live in the suite's VFS settings store.
void MainComponent::loadLayouts()
{
    juce::String error;
    const auto stored = creation::services::SuiteVfsJsonStore::loadJson(layoutStorePath, error);
    if (! stored.isObject())
        return;

    if (stored["materials"].isObject())
        materialsDock->applyLayout(stored["materials"]);
    if (stored["imageLab"].isObject())
        imageLabDock->applyLayout(stored["imageLab"]);
    currentArea = stored["workArea"].toString() == "imageLab" ? WorkArea::imageLab : WorkArea::materials;
}

void MainComponent::saveLayouts()
{
    if (materialsDock == nullptr || imageLabDock == nullptr)
        return;

    auto* state = new juce::DynamicObject();
    state->setProperty("workArea", currentArea == WorkArea::imageLab ? "imageLab" : "materials");
    state->setProperty("materials", materialsDock->captureLayout());
    state->setProperty("imageLab", imageLabDock->captureLayout());
    juce::String error;
    creation::services::SuiteVfsJsonStore::saveJson(layoutStorePath, juce::var(state), error);
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
