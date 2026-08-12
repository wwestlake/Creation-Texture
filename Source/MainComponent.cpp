#include "MainComponent.h"

#include <thread>

#include "Branding.h"
#include "../Language/AppLanguagePolicy.h"
#include <creation/ui/CreationSuiteLogos.h>

namespace
{
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
}

MainComponent::MainComponent()
{
    configureHeader();
    configurePanels();
    configureAiPanel();
    loadSuiteState();
    processRegistration.RegisterSelf("texture");
    setSize(1380, 860);
}
MainComponent::~MainComponent() = default;

void MainComponent::configureHeader()
{
    headerBar.setAppTitle("Creation Texture");
    headerBar.setLogoImage(creation::ui::getSuiteLogoImage(creation::ui::SuiteLogoId::texture));
    headerBar.setProjectLabel("Shell: Ready for domain implementation");
    headerBar.setTransportControlsVisible(false);
    headerBar.audioButton.setButtonText("Refresh");
    headerBar.tourButton.setButtonText("EULA");
    headerBar.setStatusText("Loading shared suite state...");
    headerBar.onAudioRequested = [this]
    {
        loadSuiteState();
    };
    headerBar.onTourRequested = [this]
    {
        suiteShellController.showSuiteEula();
    };
    suiteShellController.attach(headerBar,
                                {
                                    "Creation Texture",
                                    creation::assets::SuiteAppDomain::texture,
                                    creation_texture::branding::backgroundColour()
                                },
                                [this](const juce::String& status)
                                {
                                    headerBar.setStatusText(status);
                                    if (status.containsIgnoreCase("saved suite-wide"))
                                        loadSuiteState();
                                });

    addAndMakeVisible(headerBar);
}

void MainComponent::configurePanels()
{
    titleLabel.setText("Creation Texture", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(31.0f, juce::Font::bold));
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(titleLabel);

    subtitleLabel.setText("Shared suite shell with AI, configuration, registry, and domain-entry wiring already in place.",
                          juce::dontSendNotification);
    subtitleLabel.setColour(juce::Label::textColourId, juce::Colour(0xffc9d3e3));
    addAndMakeVisible(subtitleLabel);

    runtimeLabel.setText(juce::String(creation_texture::language::getLanguageRuntimeSummary()), juce::dontSendNotification);
    runtimeLabel.setColour(juce::Label::textColourId, creation_texture::branding::accentColour());
    addAndMakeVisible(runtimeLabel);

    workbenchGroup.setText("Domain Workbench");
    resourcesGroup.setText("Resources And Registry");
    configGroup.setText("Suite Configuration");

    addAndMakeVisible(workbenchGroup);
    addAndMakeVisible(resourcesGroup);
    addAndMakeVisible(configGroup);

    configureSummaryBox(workbenchSummary);
    configureSummaryBox(resourcesSummary);
    configureSummaryBox(configSummary);

    addAndMakeVisible(workbenchSummary);
    addAndMakeVisible(resourcesSummary);
    addAndMakeVisible(configSummary);
}

void MainComponent::configureAiPanel()
{
    addAndMakeVisible(aiPanel);

    aiPanel.onAccountChanged = [this](const juce::String& accountId)
    {
        if (const auto* account = creation::services::SuiteAiSettingsResolver::findAccountById(suiteAiSettings, accountId))
        {
            resolvedAiSettings.accountId = account->accountId;
            resolvedAiSettings.providerId = account->providerId;
            resolvedAiSettings.baseUrl = account->baseUrl;
            resolvedAiSettings.modelName = account->modelName;
            resolvedAiSettings.apiKey = account->apiKey;
            aiPanel.setSelectedModel(resolvedAiSettings.modelName);
        }
    };

    aiPanel.onModelChanged = [this](const juce::String& modelName)
    {
        resolvedAiSettings.modelName = modelName.trim();
    };

    aiPanel.onPromptSubmitted = [this](const juce::String& submittedPrompt)
    {
        pendingAiPrompt = submittedPrompt;

        creation::services::SuiteContextRetrievalRequest request;
        request.prompt = submittedPrompt;
        request.appDomain = juce::String(creation_texture::language::getAppDomainName());
        request.maxItems = 6;
        request.processInstruction.steeringNote = aiPanel.getSteeringNote();

        contextEngine.SubmitRequest(request);
        headerBar.setStatusText("Building AI context packet...");
    };

    aiPanel.onCollapsedChanged = [this](bool)
    {
        resized();
    };

    contextEngine.onContextReady = [this](const creation::services::SuiteContextPacket& packet)
    {
        juce::MessageManager::callAsync([this, packet]
        {
            aiPanel.setContextPacket(packet);
            if (pendingAiPrompt.isNotEmpty() && ! aiCompletionInFlight)
                launchAiCompletion(packet);
        });
    };
}

void MainComponent::launchAiCompletion(const creation::services::SuiteContextPacket& packet)
{
    if (aiCompletionInFlight)
        return;

    if (! resolvedAiSettings.isValid() || resolvedAiSettings.apiKey.isEmpty())
    {
        aiPanel.setAssistantResponse("Configure an AI account in Suite Settings first.");
        headerBar.setStatusText("No usable AI account configured.");
        return;
    }

    if (pendingAiPrompt.trim().isEmpty())
        return;

    aiCompletionInFlight = true;
    aiPanel.clearSteeringNote();

    auto userPrompt = pendingAiPrompt;
    juce::String contextBlock;
    if (! packet.snippets.isEmpty())
    {
        contextBlock << "Project context (use only if directly relevant to the request below):\n";
        for (const auto& snippet : packet.snippets)
            contextBlock << "- " << snippet.title << " (" << snippet.category << "): " << snippet.excerpt << "\n";
        contextBlock << "\n";
    }
    userPrompt = contextBlock + userPrompt;

    std::thread([safeThis = juce::Component::SafePointer<MainComponent>(this),
                 settings = resolvedAiSettings,
                 userPrompt = std::move(userPrompt)]() mutable
    {
        if (safeThis == nullptr)
            return;

        creation::services::SuiteAiChatClient::ChatResult result;
        auto ok = safeThis->aiChatClient.sendChatCompletion(settings, juce::String(), userPrompt, result);

        juce::MessageManager::callAsync([safeThis, ok, result = std::move(result)]() mutable
        {
            if (safeThis == nullptr)
                return;

            safeThis->aiCompletionInFlight = false;
            safeThis->pendingAiPrompt.clear();

            if (ok)
            {
                safeThis->aiPanel.setAssistantResponse(result.text);
                safeThis->headerBar.setStatusText("AI response received.");
            }
            else
            {
                safeThis->aiPanel.setAssistantResponse(result.errorMessage);
                safeThis->headerBar.setStatusText("AI request failed: " + result.errorMessage);
            }
        });
    }).detach();
}

void MainComponent::loadSuiteState()
{
    juce::String suiteError;
    suiteSettings = suiteSettingsStore.load(suiteError);

    juce::String aiError;
    suiteAiSettings = suiteAiSettingsStore.load(aiError);
    resolvedAiSettings = creation::services::SuiteAiSettingsResolver::resolveRuntimeSettingsForApp(suiteAiSettings, currentDomain());
    aiPanel.RefreshConfiguredAccounts();
    aiPanel.setSelectedAccountId(resolvedAiSettings.accountId);
    aiPanel.setSelectedModel(resolvedAiSettings.modelName);

    juce::String registryError;
    const auto allProjects = creation::interop::ProjectRegistry::discoverProjects(suiteSettings, registryError);
    totalProjectCount = allProjects.size();

    creation::interop::ProjectQuery domainQuery;
    domainQuery.appDomain = currentDomain();
    const auto domainProjects = creation::interop::ProjectRegistry::queryProjects(suiteSettings, domainQuery, registryError);
    domainProjectCount = domainProjects.size();
    lastRegistryError = registryError;

    if (suiteError.isNotEmpty())
        headerBar.setStatusText("Suite settings: " + suiteError);
    else if (aiError.isNotEmpty())
        headerBar.setStatusText("AI settings: " + aiError);
    else if (registryError.isNotEmpty())
        headerBar.setStatusText("Project registry: " + registryError);
    else
        headerBar.setStatusText("Shared suite shell ready.");

    refreshShellSummary();
}

void MainComponent::refreshShellSummary()
{
    workbenchSummary.setText(workbenchSummaryText(), juce::dontSendNotification);
    resourcesSummary.setText(registrySummaryText(), juce::dontSendNotification);
    configSummary.setText(configSummaryText(), juce::dontSendNotification);
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
    text << "Projects in this domain: " << domainProjectCount << "\n";
    text << "Projects across all known suite domains: " << totalProjectCount << "\n\n";
    text << "This scaffold is already connected to the shared project registry layer.\n";
    text << "Use this panel to confirm the app is seeing the same suite storage model as every other project.\n";

    if (lastRegistryError.isNotEmpty())
        text << "\nRegistry message: " << lastRegistryError;

    return text;
}

juce::String MainComponent::configSummaryText() const
{
    const auto configDirectory = suiteSettingsStore.getSuiteConfigDirectory().getFullPathName();
    const auto containersDirectory = creation::suite::getProjectContainerDirectory(suiteSettings).getFullPathName();

    juce::String text;
    text << "Suite config directory: " << configDirectory << "\n";
    text << "Project container root: " << containersDirectory << "\n";
    text << "Suite VFS root: " << suiteSettings.suiteVfsRoot << "\n\n";
    text << "Use the suite gear button to manage shared settings without rebuilding this app-specific shell.";
    return text;
}

juce::String MainComponent::workbenchSummaryText() const
{
    juce::String text;
    text << "Project shell target: Creation Texture\n";
    text << "Domain token: " << juce::String(creation_texture::language::getAppDomainName()) << "\n\n";
    text << "Recommended next moves:\n";
    text << "- define the app's core workflows in docs/CAPABILITIES.md\n";
    text << "- replace this panel with the first real domain surface\n";
    text << "- keep app-specific logic in Source/ and Language/\n";
    text << "- consume shared suite libraries instead of copying infrastructure\n";
    return text;
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
    headerBar.setBounds(getLocalBounds().removeFromTop(96));

    auto area = getLocalBounds().reduced(34, 28);
    area.removeFromTop(88);

    titleLabel.setBounds(area.removeFromTop(38));
    subtitleLabel.setBounds(area.removeFromTop(26));
    runtimeLabel.setBounds(area.removeFromTop(24));
    area.removeFromTop(16);

    auto topRow = area.removeFromTop(area.getHeight() / 2);
    auto leftTop = topRow.removeFromLeft(topRow.getWidth() / 2);
    leftTop.removeFromRight(8);
    topRow.removeFromLeft(8);

    workbenchGroup.setBounds(leftTop);
    resourcesGroup.setBounds(topRow);

    auto bottomRow = area;
    auto leftBottom = bottomRow.removeFromLeft(bottomRow.getWidth() / 2);
    leftBottom.removeFromRight(8);
    bottomRow.removeFromLeft(8);

    aiPanel.setBounds(leftBottom);
    configGroup.setBounds(bottomRow);

    workbenchSummary.setBounds(workbenchGroup.getBounds().reduced(14, 26));
    resourcesSummary.setBounds(resourcesGroup.getBounds().reduced(14, 26));
    configSummary.setBounds(configGroup.getBounds().reduced(14, 26));
}

