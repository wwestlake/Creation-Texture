#include "ProceduralWorkspacePanel.h"
#include "TextureNodeCatalog.h"
#include "ViewerNodeEditor.h"
#include "ContrastAdjustmentEditor.h"
#include "TextureCompiler.h"

ProceduralWorkspacePanel::ProceduralWorkspacePanel()
    : registry(creation_texture::nodes::BuildTextureNodeCatalog()),
      graph("Procedural", ce::node_system::GraphTarget::Dataflow),
      graphComponent(graph, registry.TypeRegistry()),
      paletteComponent(registry.TypeRegistry())
{
    addAndMakeVisible(paletteComponent);
    addAndMakeVisible(graphComponent);
    addAndMakeVisible(frusty);
    
    addAndMakeVisible(compileButton);
    compileButton.onClick = [this]() {
        auto result = creation_texture::CompileTextureGraph(graph, registry.TypeRegistry());
        if (result.ok) {
            currentSnapshot = std::make_shared<TextureFrameSnapshot>();
            currentSnapshot->generatedGlsl = result.source.evaluateFunction;
            currentSnapshot->debugColour = juce::Colour(0xff121212);
            
            for (auto& viewer : activeViewers) {
                if (viewer != nullptr) viewer->setSnapshot(currentSnapshot);
            }
        } else {
            juce::String errs = "Compile Failed:\n";
            for (const auto& err : result.errors) errs += juce::String(err) + "\n";
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Compiler Error", errs);
        }
    };

    graphComponent.onNodeDoubleClicked = [this](ce::node_system::NodeId id, juce::Rectangle<float> bounds) {
        handleNodeDoubleClicked(id, bounds);
    };
    
    currentSnapshot = std::make_shared<TextureFrameSnapshot>();
    currentSnapshot->debugColour = juce::Colour(0xff121212);
    currentSnapshot->generatedGlsl = "void EvaluateTexture(in vec2 vUV, out vec4 outColor) { outColor = vec4(vUV.x, vUV.y, 1.0, 1.0); }";
}

ProceduralWorkspacePanel::~ProceduralWorkspacePanel() = default;

void ProceduralWorkspacePanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2227)); // Background
}

void ProceduralWorkspacePanel::resized()
{
    auto bounds = getLocalBounds();
    paletteComponent.setBounds(bounds.removeFromLeft(250));
    
    auto topBar = bounds.removeFromTop(40);
    compileButton.setBounds(topBar.reduced(4).removeFromLeft(120));
    
    graphComponent.setBounds(bounds);
    
    frusty.setBounds(getLocalBounds().getRight() - 140, getLocalBounds().getBottom() - 140, 120, 120);
}

bool ProceduralWorkspacePanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".png") || f.endsWithIgnoreCase(".jpg") || f.endsWithIgnoreCase(".jpeg"))
            return true;
    }
    return false;
}

void ProceduralWorkspacePanel::fileDragEnter(const juce::StringArray& files, int x, int y) {}
void ProceduralWorkspacePanel::fileDragMove(const juce::StringArray& files, int x, int y) {}
void ProceduralWorkspacePanel::fileDragExit(const juce::StringArray& files) {}

void ProceduralWorkspacePanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".png") || file.endsWithIgnoreCase(".jpg") || file.endsWithIgnoreCase(".jpeg"))
        {
            auto& node = graph.AddNode(creation_texture::nodes::NodeType::ImageInput, ce::node_system::Domain::Material);
            node.SetEditorPosition(0.0f, 0.0f);
        }
    }
    graphComponent.repaint();
}

class EditorDocumentWindow : public juce::DocumentWindow {
public:
    EditorDocumentWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        setDropShadowEnabled(true);
    }
    void closeButtonPressed() override { delete this; }
};

void ProceduralWorkspacePanel::handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds)
{
    auto* node = graph.FindNode(id);
    if (!node) return;

    if (node->TypeName() == creation_texture::nodes::NodeType::Viewer)
    {
        auto viewer = new ViewerNodeEditor();
        viewer->setSnapshot(currentSnapshot);
        activeViewers.add(viewer);
        
        auto window = new EditorDocumentWindow("Viewer", viewer);
        window->setBounds(juce::Rectangle<int>(400, 400).withCentre(localPointToGlobal(bounds.getCentre()).toInt()));
        window->setVisible(true);
    }
    else if (node->TypeName() == creation_texture::nodes::NodeType::ContrastAdjustment)
    {
        auto window = new EditorDocumentWindow("Contrast Adjustment", new ContrastAdjustmentEditor());
        window->setBounds(juce::Rectangle<int>(350, 350).withCentre(localPointToGlobal(bounds.getCentre()).toInt()));
        window->setVisible(true);
    }
}
