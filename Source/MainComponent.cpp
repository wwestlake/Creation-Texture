#include "MainComponent.h"
#include "Branding.h"
#include <creation/assets/ProjectWorkspaceService.h>
#include <creation/assets/ProjectContainerService.h>
#include <creation/assets/ProjectAssetService.h>
#include <creation/services/SuiteVfsJsonStore.h>
#include <creation/ui/SuiteJUCEApplication.h>

MainComponent::MainComponent()
{
    // Progress bars fill in blue, so progress is easy to see (owner, 2026-09-30).
    juce::LookAndFeel::getDefaultLookAndFeel().setColour(juce::ProgressBar::foregroundColourId, juce::Colour(0xff3b82f6));

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

    // Work areas are chosen from the Layout menu; each swaps in its own whole layout.
    using Zone = CreationDock::DockTargetZone;
    imageLabDock = std::make_unique<CreationDock::DockManager>(*this);
    imageLabDock->registerPanel("Canvas", "Canvas", std::make_unique<NonOwningPanelHost>(imageLab.getCanvas()), Zone::CenterTab);
    imageLabDock->registerPanel("Layers", "Layers", std::make_unique<NonOwningPanelHost>(imageLab.getLayersPanel()), Zone::Right);
    imageLabDock->registerPanel("History", "History", std::make_unique<NonOwningPanelHost>(imageLab.getHistoryPanel()), Zone::Right);
    imageLabDock->activatePanel("Layers");
    addChildComponent(imageLabDock.get());

    surfaceMapDock = std::make_unique<CreationDock::DockManager>(*this);
    surfaceMapDock->registerPanel("SurfaceSettings", "Settings", std::make_unique<NonOwningPanelHost>(surfaceMap.getSettingsPanel()), Zone::Left);
    surfaceMapDock->registerPanel("SurfaceMaps", "Maps", std::make_unique<NonOwningPanelHost>(surfaceMap.getMapsPanel()), Zone::CenterTab);
    surfaceMapDock->registerPanel("SurfacePreview", "3D Preview", std::make_unique<NonOwningPanelHost>(surfaceMap.getPreview()), Zone::Right);
    addChildComponent(surfaceMapDock.get());

    // Graph and Draw are two views of the same Image Graph: they share its nodes, graph, Variables and Properties.
    auto shared = [this](WorkArea area, juce::Component& content) {
        auto host = std::make_unique<NonOwningPanelHost>(content);
        sharedHosts.push_back({ area, host.get() });
        return host;
    };
    graphDock = std::make_unique<CreationDock::DockManager>(*this);
    graphDock->registerPanel("GraphNodes", "Nodes", shared(WorkArea::graph, graphEditor.getPalette()), Zone::Left);
    graphDock->registerPanel("GraphVariables", "Variables", shared(WorkArea::graph, graphEditor.getVariablesPanel()), Zone::Left);
    graphDock->registerPanel("GraphTypes", "Types", shared(WorkArea::graph, graphEditor.getTypesPanel()), Zone::Left);
    graphDock->registerPanel("ImageGraph", "Image Graph", shared(WorkArea::graph, graphEditor.getGraphView()), Zone::CenterTab);
    graphDock->registerPanel("GraphPreview", "2D Preview", std::make_unique<NonOwningPanelHost>(graphEditor.getPreview()), Zone::Right);
    graphDock->registerPanel("GraphMaterialPreview", "3D Preview", std::make_unique<NonOwningPanelHost>(graphEditor.getMaterialPreview()), Zone::Right);
    graphDock->registerPanel("GraphProperties", "Properties", shared(WorkArea::graph, graphEditor.getPropertiesPanel()), Zone::Right);
    graphDock->registerPanel("GraphEngineer", "Virtual Engineer", shared(WorkArea::graph, engineerChat), Zone::Right);
    graphDock->registerPanel("GraphCards", "Cards", shared(WorkArea::graph, cardsPanel), Zone::Right);
    graphDock->activatePanel("GraphPreview");
    graphDock->activatePanel("GraphNodes");
    addChildComponent(graphDock.get());

    // Draw (requirements section 5): laid out like the other tools - the graph in the centre, the canvas as the
    // preview on the right with Properties, the script with Nodes and Variables on the left.
    drawDock = std::make_unique<CreationDock::DockManager>(*this);
    drawDock->registerPanel("DrawScript", "Script", std::make_unique<NonOwningPanelHost>(graphEditor.getScriptPanel()), Zone::Left);
    drawDock->registerPanel("DrawNodes", "Nodes", shared(WorkArea::draw, graphEditor.getPalette()), Zone::Left);
    drawDock->registerPanel("DrawVariables", "Variables", shared(WorkArea::draw, graphEditor.getVariablesPanel()), Zone::Left);
    drawDock->registerPanel("DrawTypes", "Types", shared(WorkArea::draw, graphEditor.getTypesPanel()), Zone::Left);
    drawDock->registerPanel("DrawGraph", "Image Graph", shared(WorkArea::draw, graphEditor.getGraphView()), Zone::CenterTab);
    drawDock->registerPanel("DrawCanvas", "Canvas", std::make_unique<NonOwningPanelHost>(graphEditor.getDrawCanvas()), Zone::Right);
    drawDock->registerPanel("DrawProperties", "Properties", shared(WorkArea::draw, graphEditor.getPropertiesPanel()), Zone::Right);
    drawDock->registerPanel("DrawEngineer", "Virtual Engineer", shared(WorkArea::draw, engineerChat), Zone::Right);
    drawDock->registerPanel("DrawCards", "Cards", shared(WorkArea::draw, cardsPanel), Zone::Right);
    drawDock->activatePanel("DrawScript");
    drawDock->activatePanel("DrawCanvas");
    addChildComponent(drawDock.get());

    loadLayouts();
    showWorkArea(currentArea);

    setSize(1600, 1000);

    projectImages.setProjectSession(&projectSession);

    imageLab.setProjectSession(&projectSession);
    imageLab.setImageSource(projectImages.source());
    imageLab.onStatus = [this](const juce::String& text) { headerBar.setStatusText(text); };

    surfaceMap.setProjectSession(&projectSession);
    surfaceMap.setImageSource(projectImages.source());
    surfaceMap.onStatus = [this](const juce::String& text) { headerBar.setStatusText(text); };

    graphEditor.setProjectSession(&projectSession);
    graphEditor.setImageSource(projectImages.source());
    graphEditor.onStatus = [this](const juce::String& text) {
        headerBar.setStatusText(text);
        refreshTitle();
    };
    // A material shows on the 3D Preview, an image graph on the 2D Preview.
    graphEditor.onTypeChanged = [this]() {
        graphDock->activatePanel(graphEditor.isMaterial() ? "GraphMaterialPreview" : "GraphPreview");
        menuItemsChanged();
        refreshTitle();
    };

    juce::String error;
    ensureProjectSessionActive(error);
    graphEditor.projectOpened();
    refreshTitle();

    // The Virtual Engineer: what is open goes with every request; it acts on the graph with the editor's tools; the API
    // shows the graph, its types and its errors.
    engineer.appContext = [this](const juce::String&) { return graphEditor.describeForAgent(); };
    graphEditor.registerAgentTools(engineer);
    agentApi.addAppEndpoint("graph", "The open graph: its nodes, their errors, and its exact saved text (frgraph)",
                            [this] { return graphEditor.graphForAgent(); });
    agentApi.addAppEndpoint("types", "The enums and structs in scope: the graph's own, the project's, built-in",
                            [this] { return graphEditor.typesForAgent(); });
    agentApi.addAppEndpoint("errors", "The nodes that fail now, with their messages", [this] { return graphEditor.errorsForAgent(); });
    projectChangedForEngineer();
    juce::String apiError;
    if (! agentApi.start(apiError))
        headerBar.setStatusText("The Virtual Engineer API did not start: " + apiError);
}

void MainComponent::projectChangedForEngineer()
{
    engineer.setProjectId(projectSession.isValid() ? projectSession.getManifest().projectId : juce::String());
    engineer.clearConversation();
    cardsPanel.refresh();
}

MainComponent::~MainComponent()
{
    agentApi.stop();
    saveLayouts();
    menuBar.reset();
    imageLabDock.reset();
    surfaceMapDock.reset();
    graphDock.reset();
    drawDock.reset();
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
    
    projectImages.clear();
    graphEditor.projectOpened();
    graphEditor.newGraph(image_graph::kImageDiagram);
    projectChangedForEngineer();
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
    imageLabDock->setBounds(bounds);
    surfaceMapDock->setBounds(bounds);
    graphDock->setBounds(bounds);
    drawDock->setBounds(bounds);
}

juce::StringArray MainComponent::getMenuBarNames()
{
    if (currentArea == WorkArea::imageLab)
        return { "File", "Edit", "Layer", "View", "Layout", "Help" };
    if (currentArea == WorkArea::surfaceMap || currentArea == WorkArea::graph || currentArea == WorkArea::draw)
        return { "File", "View", "Layout", "Help" };
    return { "File", "View", "Layout", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int, const juce::String& menuName)
{
    juce::PopupMenu menu;
    const bool project = projectSession.isValid();
    const bool imageLabArea = currentArea == WorkArea::imageLab;
    const bool surfaceArea = currentArea == WorkArea::surfaceMap;
    const bool drawArea = currentArea == WorkArea::draw;
    const bool graphArea = currentArea == WorkArea::graph || drawArea; // Draw is a view of the graph being edited
    const bool imageGraphOpen = ! graphEditor.isMaterial();
    auto& layers = imageLab.getDocument();

    if (menuName == "File" && graphArea)
    {
        menu.addItem(70, "New Image Graph");
        menu.addItem(69, "New Material");
        menu.addItem(71, "Open...", graphEditor.hasProject());
        menu.addSeparator();
        menu.addItem(72, "Save", graphEditor.hasProject());
        menu.addItem(73, "Save As...", graphEditor.hasProject());
        menu.addSeparator();
        menu.addItem(79, "Render Outputs", graphEditor.hasProject() && imageGraphOpen);
        menu.addItem(74, "Save Previewed Output as Image...", imageGraphOpen && graphEditor.canSaveOutput());
    }
    else if (menuName == "File" && surfaceArea)
    {
        menu.addItem(8, "New Surface Map");
        menu.addItem(7, "Open Surface Map...", surfaceMap.canOpen());
        menu.addSeparator();
        menu.addItem(6, "Save Surface Map...", surfaceMap.canSave());
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
        if (drawArea)
        {
            menu.addItem(90, "Canvas");
            menu.addItem(91, "Script");
            menu.addItem(92, "Nodes");
            menu.addItem(93, "Variables");
            menu.addItem(96, "Types");
            menu.addItem(94, "Image Graph");
            menu.addItem(95, "Properties");
        }
        else if (graphArea)
        {
            menu.addItem(75, "Nodes");
            menu.addItem(76, "Graph");
            menu.addItem(77, "2D Preview");
            menu.addItem(81, "3D Preview");
            menu.addItem(82, "Types");
            menu.addItem(78, "Properties");
            menu.addItem(80, "Variables");
        }
        else if (surfaceArea)
        {
            menu.addItem(60, "Settings");
            menu.addItem(61, "Maps");
            menu.addItem(62, "3D Preview");
        }
        else if (imageLabArea)
        {
            menu.addItem(25, "Canvas");
            menu.addItem(26, "Layers");
            menu.addItem(27, "History");
        }
    }
    else if (menuName == "Layout")
    {
        menu.addItem(51, "Image Lab", true, imageLabArea);
        menu.addItem(52, "Surface Map", true, surfaceArea);
        menu.addItem(53, "Graph", true, currentArea == WorkArea::graph);
        menu.addItem(54, "Draw", true, drawArea);
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
        case 5: imageLab.saveImage(); break;
        case 6: surfaceMap.saveSurfaceMap(); break;
        case 7: surfaceMap.openSurfaceMap(); break;
        case 8: surfaceMap.newSurfaceMap(); break;
        case 52: showWorkArea(WorkArea::surfaceMap); break;
        case 53: showWorkArea(WorkArea::graph); break;
        case 54: showWorkArea(WorkArea::draw); break;
        case 90: dock.activatePanel("DrawCanvas"); break;
        case 91: dock.activatePanel("DrawScript"); break;
        case 92: dock.activatePanel("DrawNodes"); break;
        case 93: dock.activatePanel("DrawVariables"); break;
        case 94: dock.activatePanel("DrawGraph"); break;
        case 95: dock.activatePanel("DrawProperties"); break;
        case 69: confirmDiscardingEdits([this]() { graphEditor.newGraph(ce::material::kMaterialDiagram); }); break;
        case 70: confirmDiscardingEdits([this]() { graphEditor.newGraph(image_graph::kImageDiagram); }); break;
        case 71: confirmDiscardingEdits([this]() { graphEditor.openGraph(); }); break;
        case 81: dock.activatePanel("GraphMaterialPreview"); break;
        case 82: dock.activatePanel("GraphTypes"); break;
        case 96: dock.activatePanel("DrawTypes"); break;
        case 72: graphEditor.saveGraph(); break;
        case 73: graphEditor.saveGraphAs(); break;
        case 74: graphEditor.saveOutputAsImage(); break;
        case 79: graphEditor.renderOutputs(); break;
        case 75: dock.activatePanel("GraphNodes"); break;
        case 76: dock.activatePanel("ImageGraph"); break;
        case 77: dock.activatePanel("GraphPreview"); break;
        case 78: dock.activatePanel("GraphProperties"); break;
        case 80: dock.activatePanel("GraphVariables"); break;
        case 60: dock.activatePanel("SurfaceSettings"); break;
        case 61: dock.activatePanel("SurfaceMaps"); break;
        case 62: dock.activatePanel("SurfacePreview"); break;
        case 10: imageLab.undo(); break;
        case 11: imageLab.redo(); break;
        case 25: dock.activatePanel("Canvas"); break;
        case 26: dock.activatePanel("Layers"); break;
        case 27: dock.activatePanel("History"); break;
        case 29: dock.resetLayout(); saveLayouts(); break;
        case 31:
            if (auto* app = dynamic_cast<creation::ui::SuiteJUCEApplication*>(juce::JUCEApplication::getInstance()))
                app->showAboutBox();
            break;
        case 51: showWorkArea(WorkArea::imageLab); break;
        case 40: imageLab.addImageLayer(); break;
        case 41: imageLab.getDocument().duplicateActive(); break;
        case 42: imageLab.getDocument().removeActive(); break;
        default: break;
    }
}

CreationDock::DockManager& MainComponent::dockFor(WorkArea area)
{
    if (area == WorkArea::surfaceMap)
        return *surfaceMapDock;
    if (area == WorkArea::graph)
        return *graphDock;
    if (area == WorkArea::draw)
        return *drawDock;
    return *imageLabDock;
}

void MainComponent::showWorkArea(WorkArea area)
{
    currentArea = area;
    imageLabDock->setVisible(area == WorkArea::imageLab);
    surfaceMapDock->setVisible(area == WorkArea::surfaceMap);
    graphDock->setVisible(area == WorkArea::graph);
    drawDock->setVisible(area == WorkArea::draw);
    for (auto& [hostArea, host] : sharedHosts)
        if (hostArea == area)
            host->adopt();
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

    if (stored["imageLab"].isObject())
        imageLabDock->applyLayout(stored["imageLab"]);
    if (stored["surfaceMap"].isObject())
        surfaceMapDock->applyLayout(stored["surfaceMap"]);
    if (stored["graph"].isObject())
        graphDock->applyLayout(stored["graph"]);
    if (stored["draw"].isObject())
        drawDock->applyLayout(stored["draw"]);
    const auto area = stored["workArea"].toString();
    currentArea = area == "imageLab" ? WorkArea::imageLab
                : area == "surfaceMap" ? WorkArea::surfaceMap
                : area == "draw" ? WorkArea::draw
                : WorkArea::graph;
}

void MainComponent::saveLayouts()
{
    if (imageLabDock == nullptr || surfaceMapDock == nullptr || graphDock == nullptr || drawDock == nullptr)
        return;

    auto* state = new juce::DynamicObject();
    state->setProperty("workArea", currentArea == WorkArea::imageLab ? "imageLab"
                                     : currentArea == WorkArea::surfaceMap ? "surfaceMap"
                                     : currentArea == WorkArea::draw ? "draw" : "graph");
    state->setProperty("imageLab", imageLabDock->captureLayout());
    state->setProperty("surfaceMap", surfaceMapDock->captureLayout());
    state->setProperty("graph", graphDock->captureLayout());
    state->setProperty("draw", drawDock->captureLayout());
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

void MainComponent::confirmDiscardingEdits(std::function<void()> proceed)
{
    if (! graphEditor.hasUnsavedEdits())
    {
        proceed();
        return;
    }

    const auto materialName = graphEditor.getTitle().trimCharactersAtEnd(" *");
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
    headerBar.setProjectLabel("Project: " + projectName + "  |  " + graphEditor.getTitle());
}
