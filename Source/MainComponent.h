#pragma once

#include <JuceHeader.h>
#include <CreationDock/DockManager.h>
#include "ProjectImages.h"
#include "ImageLabPanel.h"
#include "SurfaceMapPanel.h"
#include "GraphWorkspace.h"
#include <creation/ui/SuiteShellController.h>
#include <creation/ui/CreationSuiteHeaderBar.h>
#include <creation/assets/ProjectSession.h>
#include <creation/agent/AgentApi.h>
#include <creation/agent/CardsPanel.h>
#include <creation/agent/EngineerChat.h>

class MainComponent final : public juce::Component, public juce::DragAndDropContainer,
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
    bool ensureProjectSessionActive(juce::String& errorMessage);

    void confirmDiscardingEdits(std::function<void()> proceed);
    void refreshTitle();

    // Work areas: each has its own dock layout, panels and menus (docs/REQUIREMENTS.md section 4).
    // graph: the one Graph editor (image graphs and materials, shared/NodeSystem/GRAPH_TYPES.md); draw: its Draw view.
    enum class WorkArea { imageLab, surfaceMap, graph, draw };
    void showWorkArea(WorkArea area);
    CreationDock::DockManager& dockFor(WorkArea area);
    void loadLayouts();
    void saveLayouts();

    class NonOwningPanelHost : public juce::Component
    {
    public:
        explicit NonOwningPanelHost(juce::Component& contentToHost) : content(contentToHost)
        {
            addAndMakeVisible(content);
        }

        // A panel shared by two work areas (Graph and Draw) can only sit in one at a time: the area being shown takes
        // it back.
        void adopt()
        {
            if (content.getParentComponent() != this)
            {
                addAndMakeVisible(content);
                resized();
            }
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
    ProjectImages projectImages;

    ImageLabWorkspace imageLab;
    SurfaceMapWorkspace surfaceMap;
    GraphWorkspace graphEditor;

    // The suite's Virtual Engineer (shared/VirtualEngineer): its chat, its cards, and its local API with Texture's own
    // endpoints. The engineer is declared first, so it outlives everything that uses it.
    creation::agent::VirtualEngineer engineer { creation::assets::SuiteAppDomain::texture };
    creation::agent::AgentApi agentApi { engineer };
    creation::agent::EngineerChat engineerChat { engineer };
    creation::agent::CardsPanel cardsPanel { engineer };
    void projectChangedForEngineer();

    // Declared after the panels they host, so they are destroyed first.
    std::unique_ptr<juce::MenuBarComponent> menuBar;
    std::unique_ptr<CreationDock::DockManager> imageLabDock;
    std::unique_ptr<CreationDock::DockManager> surfaceMapDock;
    std::unique_ptr<CreationDock::DockManager> graphDock;
    std::unique_ptr<CreationDock::DockManager> drawDock;
    // Hosts of panels the Graph and Draw areas share, by area, so the area being shown can take its panels back.
    std::vector<std::pair<WorkArea, NonOwningPanelHost*>> sharedHosts;
    WorkArea currentArea = WorkArea::graph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

