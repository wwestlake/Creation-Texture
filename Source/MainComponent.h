#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectManifest.h>
#include <creation/interop/ProjectRegistry.h>
#include <creation/services/SuiteAiSettings.h>
#include <creation/suite/SuiteSettings.h>
#include <creation/suite/SuiteStoragePaths.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
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
    void loadSuiteState();
    void refreshShellSummary();
    creation::assets::SuiteAppDomain currentDomain() const noexcept;
    juce::String domainDisplayName() const;
    juce::String registrySummaryText() const;
    juce::String aiSummaryText() const;
    juce::String configSummaryText() const;
    juce::String workbenchSummaryText() const;

    CreationSuiteHeaderBar headerBar;
    creation::ui::SuiteShellController suiteShellController;
    juce::Label titleLabel;
    juce::Label subtitleLabel;
    juce::Label runtimeLabel;

    juce::GroupComponent workbenchGroup;
    juce::GroupComponent resourcesGroup;
    juce::GroupComponent aiGroup;
    juce::GroupComponent configGroup;

    juce::TextEditor workbenchSummary;
    juce::TextEditor resourcesSummary;
    juce::TextEditor aiSummary;
    juce::TextEditor configSummary;

    creation::suite::SuiteSettingsStore suiteSettingsStore;
    creation::services::SuiteAiSettingsStore suiteAiSettingsStore;
    creation::suite::SuiteSettings suiteSettings;
    creation::services::SuiteAiSettings suiteAiSettings;

    juce::String lastRegistryError;
    int domainProjectCount = 0;
    int totalProjectCount = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

