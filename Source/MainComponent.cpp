#include "MainComponent.h"

#include "Branding.h"
#include "../Language/AppLanguagePolicy.h"
#include <creation/ui/CreationSuiteLogos.h>
#include <gl/GL.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../../CreationEngine/third_party/stb_image.h"

using namespace juce::gl;

namespace
{
constexpr auto textureProjectRoot = "texture/";
constexpr auto textureSessionEntry = "texture/session/texture-session.xml";
constexpr auto texturePreviewImportRoot = "texture/preview/imported-textures/";
constexpr auto texturePreviewDerivedRoot = "texture/preview/derived/";
constexpr auto legacyTextureSessionEntry = "texture-session.xml";
constexpr auto legacyTexturePreviewImportRoot = "preview/imported-textures/";

void configureSummaryBox(juce::TextEditor& editor)
{
    editor.setMultiLine(true);
    editor.setReadOnly(true);
    editor.setScrollbarsShown(true);
    editor.setCaretVisible(false);
    editor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff121a24));
    editor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff314155));
    editor.setColour(juce::TextEditor::textColourId, juce::Colours::white);
}

void configureModeButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a2431));
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2b6ca3));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7e6f6));
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void configureAdjustmentSlider(juce::Slider& slider, double min, double max, double value)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
    slider.setRange(min, max, 0.01);
    slider.setValue(value, juce::dontSendNotification);
}

int workspaceModeIndex(MainComponent::WorkspaceMode mode)
{
    return static_cast<int>(mode);
}

juce::String workspaceModeName(MainComponent::WorkspaceMode mode)
{
    switch (mode)
    {
        case MainComponent::WorkspaceMode::preview: return "Preview";
        case MainComponent::WorkspaceMode::procedural: return "Procedural";
        case MainComponent::WorkspaceMode::maps: return "Maps";
        case MainComponent::WorkspaceMode::adjustments: return "Adjustments";
        case MainComponent::WorkspaceMode::utilities: return "Utilities";
    }

    return "Workspace";
}

class ManagedDocumentWindow final : public juce::DocumentWindow
{
public:
    ManagedDocumentWindow(const juce::String& title,
                          juce::Colour backgroundColour,
                          int requiredButtons,
                          std::function<void()> onCloseCallback)
        : juce::DocumentWindow(title, backgroundColour, requiredButtons),
          onClose(std::move(onCloseCallback))
    {
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        auto closeCallback = onClose;
        juce::MessageManager::callAsync([closeCallback]() mutable
        {
            if (closeCallback)
                closeCallback();
        });
    }

private:
    std::function<void()> onClose;
};
}

MainComponent::WorkspacePanel::WorkspacePanel(const juce::String& title)
{
    titleLabel.setText(title, juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions(22.0f)).boldened());
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    configureSummaryBox(summaryEditor);
    addAndMakeVisible(summaryEditor);
}

void MainComponent::WorkspacePanel::setSummaryText(const juce::String& text)
{
    summaryEditor.setText(text, juce::dontSendNotification);
}

void MainComponent::WorkspacePanel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(juce::Colour(0xff131c27));
    g.fillRoundedRectangle(bounds, 18.0f);
    g.setColour(juce::Colour(0xff314155));
    g.drawRoundedRectangle(bounds.reduced(0.5f), 18.0f, 1.0f);
}

void MainComponent::WorkspacePanel::resized()
{
    auto area = getLocalBounds().reduced(18);
    titleLabel.setBounds(area.removeFromTop(30));
    area.removeFromTop(10);
    summaryEditor.setBounds(area);
}

void MainComponent::configureHeader()
{
    headerBar.setAppTitle("Djehuti Texture");
    headerBar.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
    headerBar.setProjectLabel("Project: No active texture project");
    headerBar.setTransportControlsVisible(false);
    headerBar.audioButton.setButtonText("Save");
    headerBar.tourButton.setButtonText("Refresh");
    headerBar.setStatusText("Loading shared suite state...");
    headerBar.onAudioRequested = [this]
    {
        saveProjectState(true);
    };
    headerBar.onTourRequested = [this]
    {
        loadSuiteState();
    };

    suiteShellController.attach(headerBar,
                                {
                                    "Djehuti Texture",
                                    creation::assets::SuiteAppDomain::texture,
                                    creation_texture::branding::backgroundColour()
                                },
                                [this](const juce::String& status)
                                {
                                    headerBar.setStatusText(status);
                                });
    suiteShellController.onProjectOpenRequested = [this](const juce::String& projectId)
    {
        openProject(projectId);
    };
    headerBar.onProjectMenuRequested = [this]
    {
        suiteShellController.showProjectBrowser();
    };

    addAndMakeVisible(headerBar);
}

void MainComponent::configurePanels()
{
}
void MainComponent::updateModeButtons()
{
}
{
    modeBar.setActiveMode(activeMode);
}

void MainComponent::refreshModeVisibility()
{
    viewerPanel.setVisible(activeMode == WorkspaceMode::preview && ! isWorkspacePoppedOut(WorkspaceMode::preview));
    nodeGraphPanel.setVisible(activeMode == WorkspaceMode::procedural && ! isWorkspacePoppedOut(WorkspaceMode::procedural));
    mapsWorkspace.setVisible(activeMode == WorkspaceMode::maps && ! isWorkspacePoppedOut(WorkspaceMode::maps));
    adjustmentsWorkspace.setVisible(activeMode == WorkspaceMode::adjustments && ! isWorkspacePoppedOut(WorkspaceMode::adjustments));
    utilitiesWorkspace.setVisible(activeMode == WorkspaceMode::utilities && ! isWorkspacePoppedOut(WorkspaceMode::utilities));
    poppedWorkspacePlaceholder.setVisible(isWorkspacePoppedOut(activeMode));
}

juce::Component* MainComponent::getWorkspaceComponent(WorkspaceMode mode)
{
    switch (mode)
    {
        case WorkspaceMode::preview: return &viewerPanel;
        case WorkspaceMode::procedural: return &nodeGraphPanel;
        case WorkspaceMode::maps: return &mapsWorkspace;
        case WorkspaceMode::adjustments: return &adjustmentsWorkspace;
        case WorkspaceMode::utilities: return &utilitiesWorkspace;
    }

    return nullptr;
}

bool MainComponent::isWorkspacePoppedOut(WorkspaceMode mode) const
{
    return workspacePopoutWindows[(size_t) workspaceModeIndex(mode)] != nullptr;
}

void MainComponent::popOutActiveWorkspace()
{
    popOutWorkspace(activeMode);
}

void MainComponent::popOutWorkspace(WorkspaceMode mode, const juce::Rectangle<int>* bounds)
{
    auto& windowSlot = workspacePopoutWindows[(size_t) workspaceModeIndex(mode)];

    if (windowSlot != nullptr)
    {
        if (bounds != nullptr && ! bounds->isEmpty())
            windowSlot->setBounds(*bounds);

        windowSlot->toFront(true);
        headerBar.setStatusText(workspaceModeName(mode) + " is already popped out.");
        return;
    }

    auto* component = getWorkspaceComponent(mode);
    if (component == nullptr)
        return;

    poppedWorkspacePlaceholder.setText(workspaceModeName(mode) + " is open in its own window.\nClose that window to dock it back here.",
                                       juce::dontSendNotification);

    auto window = std::make_unique<ManagedDocumentWindow>("Djehuti Texture - " + workspaceModeName(mode),
                                                          juce::Colour(0xff10141a),
                                                          juce::DocumentWindow::closeButton
                                                              | juce::DocumentWindow::minimiseButton
                                                              | juce::DocumentWindow::maximiseButton,
                                                          [this, mode]
                                                          {
                                                              dockWorkspace(mode);
                                                          });
    window->setUsingNativeTitleBar(true);
    window->setResizable(true, true);
    window->setContentNonOwned(component, false);

    if (bounds != nullptr && ! bounds->isEmpty())
        window->setBounds(*bounds);
    else
        window->centreWithSize(1180, 760);

    window->setVisible(true);
    window->toFront(true);
    windowSlot = std::move(window);

    refreshModeVisibility();
    resized();
    headerBar.setStatusText("Popped out " + workspaceModeName(mode) + ".");
    saveLayoutState();
}

void MainComponent::dockWorkspace(WorkspaceMode mode)
{
    auto& windowSlot = workspacePopoutWindows[(size_t) workspaceModeIndex(mode)];
    if (windowSlot == nullptr)
        return;

    auto* component = getWorkspaceComponent(mode);
    windowSlot->clearContentComponent();
    windowSlot.reset();

    if (component != nullptr)
        addAndMakeVisible(component);

    if (activeMode == mode)
        poppedWorkspacePlaceholder.setVisible(false);

    setActiveMode(mode);
    resized();
    headerBar.setStatusText("Docked " + workspaceModeName(mode) + ".");
    saveLayoutState();
}

juce::ValueTree MainComponent::createLayoutState() const
{
    juce::ValueTree layout("Layout");
    layout.setProperty("format", "creation-texture-layout", nullptr);
    layout.setProperty("formatVersion", 1, nullptr);
    layout.setProperty("activeMode", workspaceModeIndex(activeMode), nullptr);

    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto bounds = window->getBounds();
        layout.setProperty("mainWindowX", bounds.getX(), nullptr);
        layout.setProperty("mainWindowY", bounds.getY(), nullptr);
        layout.setProperty("mainWindowW", bounds.getWidth(), nullptr);
        layout.setProperty("mainWindowH", bounds.getHeight(), nullptr);
    }

    for (int index = 0; index < workspaceModeCount; ++index)
    {
        auto* window = workspacePopoutWindows[(size_t) index].get();
        if (window == nullptr)
            continue;

        auto bounds = window->getBounds();
        juce::ValueTree popped("PoppedWorkspace");
        popped.setProperty("mode", index, nullptr);
        popped.setProperty("x", bounds.getX(), nullptr);
        popped.setProperty("y", bounds.getY(), nullptr);
        popped.setProperty("w", bounds.getWidth(), nullptr);
        popped.setProperty("h", bounds.getHeight(), nullptr);
        layout.addChild(popped, -1, nullptr);
    }

    return layout;
}

void MainComponent::restoreLayoutState(const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    auto savedMode = static_cast<WorkspaceMode>(juce::jlimit(0,
                                                             workspaceModeCount - 1,
                                                             (int) state.getProperty("activeMode", workspaceModeIndex(WorkspaceMode::preview))));
    activeMode = savedMode;
    updateModeButtons();

    auto mainX = (int) state.getProperty("mainWindowX", -1);
    auto mainY = (int) state.getProperty("mainWindowY", -1);
    auto mainW = (int) state.getProperty("mainWindowW", -1);
    auto mainH = (int) state.getProperty("mainWindowH", -1);
    if (mainX >= 0 && mainY >= 0 && mainW > 0 && mainH > 0)
    {
        if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
            window->setBounds(mainX, mainY, mainW, mainH);
    }

    for (int index = 0; index < workspaceModeCount; ++index)
    {
        auto child = state.getChildWithProperty("mode", index);
        if (! child.isValid())
            continue;

        auto popX = (int) child.getProperty("x", -1);
        auto popY = (int) child.getProperty("y", -1);
        auto popW = (int) child.getProperty("w", -1);
        auto popH = (int) child.getProperty("h", -1);
        juce::Rectangle<int> bounds(popX, popY, popW, popH);
        auto mode = static_cast<WorkspaceMode>(index);
        popOutWorkspace(mode, bounds.isEmpty() ? nullptr : &bounds);
    }

    refreshModeVisibility();
}

void MainComponent::saveLayoutState()
{
    auto layoutState = createLayoutState();
    if (auto xml = layoutState.createXml())
    {
        auto* root = new juce::DynamicObject();
        root->setProperty("layoutXml", xml->toString());
        juce::String errorMessage;
        creation::services::SuiteVfsJsonStore::saveJson("creation-texture-layout.json", juce::var(root), errorMessage);
    }
}

void MainComponent::loadLayoutState()
{
    juce::String errorMessage;
    auto parsed = creation::services::SuiteVfsJsonStore::loadJson("creation-texture-layout.json", errorMessage);
    auto* state = parsed.getDynamicObject();
    if (state == nullptr)
        return;

    auto layoutXmlText = state->getProperty("layoutXml").toString();
    if (layoutXmlText.isEmpty())
        return;

    auto xml = juce::parseXML(layoutXmlText);
    if (xml == nullptr)
        return;

    restoreLayoutState(juce::ValueTree::fromXml(*xml));
}

void MainComponent::createNewProject()
{
    auto* prompt = new juce::AlertWindow("Create New Texture Project",
                                         "Enter a name for your new Creation Texture project:",
                                         juce::MessageBoxIconType::QuestionIcon);
    prompt->addTextEditor("projectName", "");
    prompt->addButton("Create Project", 1);
    prompt->addButton("Cancel", 0);

    auto options = juce::Component::SafePointer<MainComponent>(this);
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([options, prompt](int result) mutable
    {
        std::unique_ptr<juce::AlertWindow> dialog(prompt);
        if (result != 1 || options == nullptr)
            return;

        auto name = dialog->getTextEditorContents("projectName").trim();
        if (name.isEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", "Project name cannot be empty.");
            return;
        }

        juce::String err;
        if (! creation::assets::ProjectWorkspaceService::createProject(options->suiteSettings,
                                                                       creation::assets::SuiteAppDomain::texture,
                                                                       name,
                                                                       "1.0.0",
                                                                       "1.0.0",
                                                                       options->projectSession,
                                                                       err))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", err);
            return;
        }

        options->viewerPanel.clearWorkingTextures();
        options->viewerPanel.setSelectedPrimitiveIndex(0);
        options->headerBar.setProjectLabel("Project: " + options->projectSession.getManifest().projectName);
        options->saveProjectState(true);
        options->saveAppSettings();
        options->refreshShellSummary();
        options->headerBar.setStatusText("Created project context: " + options->projectSession.getManifest().projectName);
    }), true);
}

void MainComponent::openProject(const juce::String& projectId)
{
    juce::String errorMessage;
    if (! creation::assets::ProjectWorkspaceService::openProject(suiteSettings, projectId, projectSession, errorMessage))
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Project Error", errorMessage);
        return;
    }

    headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
    viewerPanel.clearWorkingTextures();
    viewerPanel.setSelectedPrimitiveIndex(0);
    loadProjectState();
    saveAppSettings();
    refreshShellSummary();
    headerBar.setStatusText("Opened project context: " + projectSession.getManifest().projectName);
}

void MainComponent::saveProjectState(bool userInitiated)
{
    if (! projectSession.isValid())
    {
        if (userInitiated)
            headerBar.setStatusText("No active project context. Open or create a project first.");
        return;
    }

    juce::ValueTree state("CreationTextureProjectState");
    state.setProperty("workspaceMode", static_cast<int>(activeMode), nullptr);
    state.setProperty("appDomain", juce::String(creation_texture::language::getAppDomainName()), nullptr);
    state.setProperty("previewPrimitiveIndex", viewerPanel.getSelectedPrimitiveIndex(), nullptr);

    auto modeNotes = state.getOrCreateChildWithName("ModeNotes", nullptr);
    modeNotes.setProperty("preview", previewModeText(), nullptr);
    modeNotes.setProperty("procedural", proceduralModeText(), nullptr);
    modeNotes.setProperty("maps", mapsModeText(), nullptr);
    modeNotes.setProperty("adjustments", adjustmentsModeText(), nullptr);
    modeNotes.setProperty("utilities", utilitiesModeText(), nullptr);
    state.addChild(viewerPanel.createWorkingSetState(), -1, nullptr);

    if (auto xml = state.createXml())
    {
        auto xmlString = xml->toString();
        juce::MemoryBlock xmlBlock(xmlString.toRawUTF8(), xmlString.getNumBytesAsUTF8());
        projectSession.writeEntry(textureSessionEntry, xmlBlock);
    }

    juce::String commitError;
    if (! projectSession.commit(commitError))
    {
        headerBar.setStatusText("Could not save Texture session into the project: " + commitError);
        return;
    }

    saveAppSettings();
    if (userInitiated)
        headerBar.setStatusText("Saved Texture session into project: " + projectSession.getManifest().projectName);
}

void MainComponent::loadProjectState()
{
    if (! projectSession.isValid())
        return;

    juce::MemoryBlock sessionData;
    if (! projectSession.readEntry(textureSessionEntry, sessionData))
    {
        if (! projectSession.readEntry(legacyTextureSessionEntry, sessionData))
            return;
    }

    auto xmlString = juce::String::createStringFromData(sessionData.getData(), static_cast<int>(sessionData.getSize()));
    auto xml = juce::XmlDocument::parse(xmlString);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml(*xml);
    auto storedMode = state.getProperty("workspaceMode", static_cast<int>(WorkspaceMode::preview));
    auto modeValue = static_cast<WorkspaceMode>(juce::jlimit(0, 4, static_cast<int>(storedMode)));
    viewerPanel.setSelectedPrimitiveIndex((int) state.getProperty("previewPrimitiveIndex", 0));
    auto workingSetState = state.getChildWithName("WorkingSet");
    if (workingSetState.isValid())
    {
        viewerPanel.restoreWorkingSetState(workingSetState,
                                               [this](const juce::String& projectEntry)
                                               {
                                                   juce::MemoryBlock data;
                                                   juce::String readPath = projectEntry;
                                                   if (! projectSession.readEntry(readPath, data))
                                                   {
                                                       if (readPath.startsWith(texturePreviewImportRoot))
                                                           readPath = juce::String(legacyTexturePreviewImportRoot) + readPath.fromFirstOccurrenceOf(texturePreviewImportRoot, false, false);

                                                       if (! projectSession.readEntry(readPath, data))
                                                           return juce::Image {};
                                                   }

                                                   juce::String imageError;
                                                   return loadTextureImageFromMemory(data.getData(), data.getSize(), readPath, imageError);
                                               });
    }
    else
    {
        auto legacyPreviewTextureEntry = state.getProperty("previewTextureEntry").toString();
        viewerPanel.clearWorkingTextures();
        if (legacyPreviewTextureEntry.startsWith(legacyTexturePreviewImportRoot))
            legacyPreviewTextureEntry = juce::String(texturePreviewImportRoot) + legacyPreviewTextureEntry.fromFirstOccurrenceOf(legacyTexturePreviewImportRoot, false, false);
        if (legacyPreviewTextureEntry.isNotEmpty())
            loadPreviewTextureFromProjectEntry(legacyPreviewTextureEntry);
    }
    setActiveMode(modeValue);
}

bool MainComponent::ensureProjectSessionActive(juce::String& errorMessage)
{
    if (projectSession.isValid())
        return true;

    auto settingsState = creation::services::SuiteVfsJsonStore::loadJson("creation-texture-settings.json", errorMessage);
    if (auto* settingsObject = settingsState.getDynamicObject())
    {
        auto lastProjectId = settingsObject->getProperty("lastOpenedProjectId").toString();
        if (lastProjectId.isNotEmpty())
        {
            if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings, lastProjectId, projectSession, errorMessage))
            {
                headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
                loadProjectState();
                return true;
            }
        }
    }

    // Unfiltered by design -- projects are not owned by any app domain.
    auto availableProjects = creation::assets::ProjectContainerService::listProjects(suiteSettings, errorMessage);
    if (! availableProjects.isEmpty())
    {
        if (creation::assets::ProjectWorkspaceService::openProject(suiteSettings,
                                                                   availableProjects.getFirst().projectId,
                                                                   projectSession,
                                                                   errorMessage))
        {
            headerBar.setProjectLabel("Project: " + projectSession.getManifest().projectName);
            loadProjectState();
            return true;
        }
    }

    errorMessage = "No project context is open yet. Use the Project menu to open or create one.";
    return false;
}

void MainComponent::saveAppSettings()
{
    auto* object = new juce::DynamicObject();
    if (projectSession.isValid())
        object->setProperty("lastOpenedProjectId", projectSession.getProjectId());
    object->setProperty("activeMode", workspaceModeIndex(activeMode));

    juce::String errorMessage;
    creation::services::SuiteVfsJsonStore::saveJson("creation-texture-settings.json", juce::var(object), errorMessage);
}

void MainComponent::importPreviewTexture()
{
    previewTextureChooser = std::make_unique<juce::FileChooser>("Import Texture",
                                                                juce::File(),
                                                                "*.png;*.jpg;*.jpeg;*.bmp;*.tga");
    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    previewTextureChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& chooser)
    {
        auto file = chooser.getResult();
        previewTextureChooser.reset();

        if (! file.existsAsFile())
            return;

        importPreviewTextureFromFile(file, true);
    });
}

bool MainComponent::importPreviewTextureFromFile(const juce::File& file, bool persistIntoProject)
{
    juce::String errorMessage;
    auto image = loadTextureImageFromFile(file, errorMessage);
    if (image.isNull())
    {
        headerBar.setStatusText(errorMessage.isNotEmpty() ? errorMessage : "Could not import that texture.");
        viewerPanel.setStatusText("Texture import failed.");
        return false;
    }

    if (persistIntoProject)
    {
        if (! projectSession.isValid())
        {
            headerBar.setStatusText("Open or create a texture project before importing a project-backed preview texture.");
            return true;
        }

        juce::MemoryBlock fileBytes;
        if (! file.loadFileAsData(fileBytes))
        {
            headerBar.setStatusText("Could not read texture file from disk.");
            return false;
        }

        auto projectEntry = juce::String(texturePreviewImportRoot) + file.getFileName();
        projectSession.writeEntry(projectEntry, fileBytes);
        viewerPanel.addWorkingTexture(image, file.getFileName(), projectEntry);
        saveProjectState(false);
    }
    else
    {
        viewerPanel.addWorkingTexture(image, file.getFileName(), {});
    }

    refreshShellSummary();
    headerBar.setStatusText("Imported texture into working set: " + file.getFileName());
    return true;
}

void MainComponent::persistProcessedTexture(const juce::Image& image, const juce::String& sourceLabel)
{
    if (! projectSession.isValid() || image.isNull())
        return;

    juce::MemoryOutputStream encodedPng;
    juce::PNGImageFormat pngFormat;
    if (! pngFormat.writeImageToStream(image, encodedPng))
    {
        headerBar.setStatusText("Could not encode the FRust-processed texture as PNG.");
        return;
    }

    auto stem = juce::File::createLegalFileName(juce::File(sourceLabel).getFileNameWithoutExtension());
    if (stem.isEmpty())
        stem = "texture";

    const auto entry = juce::String(texturePreviewDerivedRoot) + stem + "-adjusted.png";
    projectSession.writeEntry(entry, encodedPng.getMemoryBlock());
    saveProjectState(false);
    headerBar.setStatusText("FRust texture plugin saved derived asset: " + entry);
}

bool MainComponent::loadPreviewTextureFromProjectEntry(const juce::String& entryPath)
{
    if (! projectSession.isValid() || entryPath.isEmpty())
        return false;

    juce::MemoryBlock data;
    if (! projectSession.readEntry(entryPath, data))
    {
        auto legacyPath = entryPath;
        if (legacyPath.startsWith(texturePreviewImportRoot))
            legacyPath = juce::String(legacyTexturePreviewImportRoot) + legacyPath.fromFirstOccurrenceOf(texturePreviewImportRoot, false, false);

        if (! projectSession.readEntry(legacyPath, data))
            return false;
    }

    juce::String errorMessage;
    auto image = loadTextureImageFromMemory(data.getData(), data.getSize(), entryPath, errorMessage);
    if (image.isNull())
    {
        headerBar.setStatusText(errorMessage.isNotEmpty() ? errorMessage : "Could not load saved preview texture.");
        return false;
    }

    viewerPanel.addWorkingTexture(image, juce::File(entryPath).getFileName(), entryPath);
    return true;
}

juce::Image MainComponent::loadTextureImageFromFile(const juce::File& file, juce::String& errorMessage) const
{
    auto image = juce::ImageFileFormat::loadFrom(file);
    if (! image.isNull())
        return image;

    juce::MemoryBlock fileBytes;
    if (! file.loadFileAsData(fileBytes))
    {
        errorMessage = "Could not read texture file from disk.";
        return {};
    }

    return loadTextureImageFromMemory(fileBytes.getData(), fileBytes.getSize(), file.getFileName(), errorMessage);
}

juce::Image MainComponent::loadTextureImageFromMemory(const void* data, size_t size, const juce::String& filenameHint, juce::String& errorMessage) const
{
    juce::MemoryInputStream stream(data, size, false);
    auto image = juce::ImageFileFormat::loadFrom(stream);
    if (! image.isNull())
        return image;

    if (! filenameHint.endsWithIgnoreCase(".tga"))
    {
        errorMessage = "Unsupported texture format. Current preview import supports PNG, JPG/JPEG, BMP, and TGA.";
        return {};
    }

    int width = 0;
    int height = 0;
    int channels = 0;
    auto* pixels = stbi_load_from_memory(static_cast<const stbi_uc*>(data),
                                         static_cast<int>(size),
                                         &width,
                                         &height,
                                         &channels,
                                         4);
    if (pixels == nullptr)
    {
        errorMessage = "Could not decode TGA texture.";
        return {};
    }

    juce::Image decoded(juce::Image::ARGB, width, height, true);
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const auto pixelIndex = ((size_t) y * (size_t) width + (size_t) x) * 4;
            decoded.setPixelAt(x, y, juce::Colour(pixels[pixelIndex + 0],
                                                  pixels[pixelIndex + 1],
                                                  pixels[pixelIndex + 2],
                                                  pixels[pixelIndex + 3]));
        }
    }

    stbi_image_free(pixels);
    return decoded;
}

creation::assets::SuiteAppDomain MainComponent::currentDomain() const noexcept
{
    return creation::assets::SuiteAppDomain::texture;
}

juce::String MainComponent::domainDisplayName() const
{
    return creation::assets::toDisplayName(currentDomain());
}

juce::String MainComponent::registrySummaryText() const
{
    juce::String text;
    text << "App domain: " << domainDisplayName() << "\n";
    // Deliberately unfiltered -- projects are not owned by any app domain,
    // see creation::interop::ProjectQuery's own comment.
    text << "Projects in the shared suite registry: " << totalProjectCount << "\n\n";
    text << "Djehuti Texture uses the shared suite project registry and VFS-backed project model.\n";
    text << "The project is the shared storage context; this tool saves its own session/assets into that project.\n";

    if (lastRegistryError.isNotEmpty())
        text << "\nRegistry message: " << lastRegistryError;

    return text;
}

juce::String MainComponent::aiSummaryText() const
{
    const auto runtime = creation::services::SuiteAiSettingsResolver::resolveRuntimeSettingsForApp(suiteAiSettings,
                                                                                                    currentDomain());

    juce::String text;
    text << "Shared AI account entries: " << suiteAiSettings.accounts.size() << "\n";
    text << "Selected app domain token: " << juce::String(creation_texture::language::getAppDomainName()) << "\n";
    text << "Resolved provider: " << runtime.providerDisplayName << "\n";
    text << "Resolved model: " << runtime.modelName << "\n";
    text << "Resolved base URL: " << runtime.baseUrl << "\n\n";
    text << "This shell is ready for Texture-specific helper workflows once the creative modes are implemented.";
    return text;
}

juce::String MainComponent::configSummaryText() const
{
    const auto configDirectory = suiteSettingsStore.getSuiteConfigDirectory().getFullPathName();
    const auto containersDirectory = creation::suite::getProjectContainerDirectory(suiteSettings).getFullPathName();
    const auto exportProjectName = projectSession.isValid() ? projectSession.getManifest().projectName : "Untitled Texture Project";
    const auto exportsDirectory = creation::suite::getExportDirectory(suiteSettings,
                                                                      currentDomain(),
                                                                      exportProjectName).getFullPathName();

    juce::String text;
    text << "Suite config directory: " << configDirectory << "\n";
    text << "Project container root: " << containersDirectory << "\n";
    text << "Suite VFS root: " << suiteSettings.suiteVfsRoot << "\n";
    text << "Project exports directory: " << exportsDirectory << "\n\n";
    text << "Canonical save/load for this tool session and its saved assets is project-backed through the suite VFS.\n";
    text << "External import/export files remain user-directed disk operations.";
    return text;
}

juce::String MainComponent::workbenchSummaryText() const
{
    juce::String text;
    if (projectSession.isValid())
        text << "Current project context: " << projectSession.getManifest().projectName << "\n";
    else
        text << "Current project context: none\n";

    text << "Current mode: ";
    switch (activeMode)
    {
        case WorkspaceMode::preview:
            text << "Preview\n\n" << previewModeText();
            break;
        case WorkspaceMode::procedural:
            text << "Procedural\n\n" << proceduralModeText();
            break;
        case WorkspaceMode::maps:
            text << "Maps\n\n" << mapsModeText();
            break;
        case WorkspaceMode::adjustments:
            text << "Adjustments\n\n" << adjustmentsModeText();
            break;
        case WorkspaceMode::utilities:
            text << "Utilities\n\n" << utilitiesModeText();
            break;
    }

    return text;
}

juce::String MainComponent::previewModeText() const
{
    return "OpenGL preview workspace.\n"
           "- preview imported textures on primitive shapes\n"
           "- preview a single loaded model with applied textures\n"
           "- foundation for the future material/texture look-dev viewport";
}

juce::String MainComponent::proceduralModeText() const
{
    return "Node-based procedural texture authoring.\n"
           "- graph-driven texture generation workflow\n"
           "- suite shell placeholder for a Blender-style procedural surface\n"
           "- intended home for reusable procedural texture graphs";
}

juce::String MainComponent::mapsModeText() const
{
    return "Map generation workspace.\n"
           "- UV-related and other derived map workflows\n"
           "- dedicated mode rather than burying map generation inside another tool\n"
           "- intended to save generated assets into the active suite project";
}

juce::String MainComponent::adjustmentsModeText() const
{
    return "Algorithmic 2D texture adjustments.\n"
           "- not a paint workflow\n"
           "- manipulation and cleanup operations such as tileability preparation\n"
           "- intended for non-destructive, repeatable texture transforms";
}

juce::String MainComponent::utilitiesModeText() const
{
    return "Derived texture utility workspace.\n"
           "- normal map preparation\n"
           "- ambient occlusion map preparation\n"
           "- light map and similar derived-output generation";
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(creation_texture::branding::backgroundColour());

    auto bounds = getLocalBounds().toFloat().reduced(18.0f);
    g.setColour(creation_texture::branding::panelColour());
    g.fillRoundedRectangle(bounds, 24.0f);

    g.setColour(creation_texture::branding::accentColour().withAlpha(0.8f));
    g.drawRoundedRectangle(bounds, 24.0f, 1.4f);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    headerBar.setBounds(bounds.removeFromTop(96));

    auto area = bounds.reduced(34, 28);

    titleLabel.setBounds(area.removeFromTop(38));
    subtitleLabel.setBounds(area.removeFromTop(26));
    runtimeLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(12);

    modeBar.setBounds(area.removeFromTop(56));
    area.removeFromTop(16);

    auto topRow = area.removeFromTop(area.getHeight() / 2);
    auto leftTop = topRow.removeFromLeft(topRow.getWidth() / 2);
    leftTop.removeFromRight(8);
    topRow.removeFromLeft(8);

    workspaceGroup.setBounds(leftTop);
    resourcesGroup.setBounds(topRow);

    auto bottomRow = area;
    auto leftBottom = bottomRow.removeFromLeft(bottomRow.getWidth() / 2);
    leftBottom.removeFromRight(8);
    bottomRow.removeFromLeft(8);

    aiGroup.setBounds(leftBottom);
    configGroup.setBounds(bottomRow);

    auto workspaceBounds = workspaceGroup.getBounds().reduced(14, 26);
    viewerPanel.setBounds(workspaceBounds);
    nodeGraphPanel.setBounds(workspaceBounds);
    mapsWorkspace.setBounds(workspaceBounds);
    adjustmentsWorkspace.setBounds(workspaceBounds);
    utilitiesWorkspace.setBounds(workspaceBounds);
    poppedWorkspacePlaceholder.setBounds(workspaceBounds);
    resourcesSummary.setBounds(resourcesGroup.getBounds().reduced(14, 26));
    aiSummary.setBounds(aiGroup.getBounds().reduced(14, 26));
    configSummary.setBounds(configGroup.getBounds().reduced(14, 26));
}
