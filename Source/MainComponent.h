#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>
#include "NodeGraphPanel.h"
#include "ViewerPanel.h"
#include <creation/ui/SuiteShellController.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/assets/ProjectSession.h>

class MainComponent final : public juce::Component,
                            public juce::MenuBarModel
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // MenuBarModel overrides
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
    void openProject(const juce::String& projectId);

    class NonOwningPanelHost : public juce::Component
    {
    public:
        explicit NonOwningPanelHost(juce::Component& contentToHost) : content(contentToHost)
        {
            addAndMakeVisible(content);
        }

        void resized() override
        {
            content.setBounds(getLocalBounds());
        }

    private:
        juce::Component& content;
    };

    CreationSuiteHeaderBar headerBar;
    creation::ui::SuiteShellController suiteShellController;
    creation::assets::ProjectSession projectSession;

    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> dockManager;

    NodeGraphPanel nodeGraphPanel;
    ViewerPanel viewerPanel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
