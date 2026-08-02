#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/interop/ProjectRegistry.h>
#include <creation/services/SuiteAiChatClient.h>
#include <creation/services/SuiteAiSettings.h>
#include <creation/services/SuiteContextEngine.h>
#include <creation/services/SuiteProcessRegistry.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/suite/SuiteStoragePaths.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/ui/SuiteAiChatPanel.h>
#include <creation/ui/SuiteShellController.h>

class MainComponent final : public juce::Component
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void configureHeader();
    void configurePanels();
    void configureAiPanel();
    void loadSuiteState();
    void refreshShellSummary();
    void launchAiCompletion(const creation::services::SuiteContextPacket& packet);
    creation::assets::SuiteAppDomain currentDomain() const noexcept;
    juce::String domainDisplayName() const;
    juce::String registrySummaryText() const;
    juce::String configSummaryText() const;
    juce::String workbenchSummaryText() const;

    CreationSuiteHeaderBar headerBar;
    creation::ui::SuiteShellController suiteShellController;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label runtimeLabel;

    juce::GroupComponent workbenchGroup;
    juce::GroupComponent resourcesGroup;
    juce::GroupComponent configGroup;

    juce::TextEditor workbenchSummary;
    juce::TextEditor resourcesSummary;
    juce::TextEditor configSummary;

    creation::ui::SuiteAiChatPanel aiPanel;
    creation::services::SuiteContextEngine contextEngine;
    creation::services::SuiteAiChatClient aiChatClient;
    creation::services::SuiteProcessRegistration processRegistration;

    creation::suite::SuiteSettingsStore suiteSettingsStore;
    creation::services::SuiteAiSettingsStore suiteAiSettingsStore;
    creation::suite::SuiteSettings suiteSettings;
    creation::services::SuiteAiSettings suiteAiSettings;
    creation::services::SuiteAiResolvedRuntimeSettings resolvedAiSettings;

    juce::String lastRegistryError;
    int domainProjectCount = 0;
    int totalProjectCount = 0;

    juce::String pendingAiPrompt;
    bool aiCompletionInFlight = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

