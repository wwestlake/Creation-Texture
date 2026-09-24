#include <creation/assets/ProjectAssetService.h>
#include "NodeGraphPanel.h"
#include "TextureNodeCatalog.h"
#include "ViewerNodeEditor.h"
#include "ContrastAdjustmentEditor.h"
#include "TextureCompiler.h"

NodeGraphPanel::NodeGraphPanel()
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
            currentSnapshot->generatedGlsl = result.source.declarations + "\n" + result.source.evaluateFunction;
            currentSnapshot->textures = result.source.textures;
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

    graphComponent.onGetNodeExtraHeight = [this](ce::node_system::NodeId id) {
        if (auto* node = graph.FindNode(id)) {
            if (node->TypeName() == creation_texture::nodes::NodeType::ImageInput)
                return 120.0f;
        }
        return 0.0f;
    };

    graphComponent.onPaintNode = [this](juce::Graphics& g, ce::node_system::NodeId id, juce::Rectangle<float> bounds) {
        if (auto* node = graph.FindNode(id)) {
            if (node->TypeName() == creation_texture::nodes::NodeType::ImageInput) {
                std::string assetPath;
                for (const auto& pin : node->Inputs()) {
                    if (pin.name == creation_texture::nodes::PinName::AssetPath) {
                        if (std::holds_alternative<std::string>(pin.defaultValue))
                            assetPath = std::get<std::string>(pin.defaultValue);
                        break;
                    }
                }
                if (!assetPath.empty() && projectSession && projectSession->isValid()) {
                    if (imagePreviewCache.find(assetPath) == imagePreviewCache.end()) {
                        juce::MemoryBlock block;
                        if (projectSession->readEntry(assetPath, block)) {
                            imagePreviewCache[assetPath] = juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());
                        } else {
                            imagePreviewCache[assetPath] = juce::Image();
                        }
                    }
                    auto img = imagePreviewCache[assetPath];
                    if (img.isValid()) {
                        auto previewBounds = bounds.withTrimmedTop(bounds.getHeight() - 110.0f).withTrimmedBottom(10.0f).reduced(10.0f, 0.0f);
                        g.drawImage(img, previewBounds, juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize);
                    }
                }
            }
        }
    };

    graphComponent.onNodeDoubleClicked = [this](ce::node_system::NodeId id, juce::Rectangle<float> bounds) {
        handleNodeDoubleClicked(id, bounds);
    };
    
    currentSnapshot = std::make_shared<TextureFrameSnapshot>();
    currentSnapshot->debugColour = juce::Colour(0xff121212);
    currentSnapshot->generatedGlsl = "void EvaluateTexture(in vec2 vUV, out vec4 outColor) { outColor = vec4(vUV.x, vUV.y, 1.0, 1.0); }";
}

NodeGraphPanel::~NodeGraphPanel() = default;

void NodeGraphPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2227)); // Background
}

void NodeGraphPanel::resized()
{
    auto bounds = getLocalBounds();
    paletteComponent.setBounds(bounds.removeFromLeft(250));
    
    auto topBar = bounds.removeFromTop(40);
    compileButton.setBounds(topBar.reduced(4).removeFromLeft(120));
    
    graphComponent.setBounds(bounds);
    
    frusty.setBounds(getLocalBounds().getRight() - 140, getLocalBounds().getBottom() - 140, 120, 120);
}

bool NodeGraphPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        if (f.endsWithIgnoreCase(".png") || f.endsWithIgnoreCase(".jpg") || f.endsWithIgnoreCase(".jpeg"))
            return true;
    }
    return false;
}

void NodeGraphPanel::fileDragEnter(const juce::StringArray& files, int x, int y) {}
void NodeGraphPanel::fileDragMove(const juce::StringArray& files, int x, int y) {}
void NodeGraphPanel::fileDragExit(const juce::StringArray& files) {}

void NodeGraphPanel::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::String err;
    if (onEnsureProjectSessionActive && !onEnsureProjectSessionActive(err))
        return;

    if (!projectSession || !projectSession->isValid())
        return;

    const auto world = graphComponent.ScreenToWorld(juce::Point<float>((float)x, (float)y));
    float currentY = world.y;

    for (const auto& file : files)
    {
        if (file.endsWithIgnoreCase(".png") || file.endsWithIgnoreCase(".jpg") || file.endsWithIgnoreCase(".jpeg"))
        {
            juce::File sourceFile(file);
            creation::assets::ProjectAssetService::ImportOptions options;
            options.kind = creation::assets::AssetKind::binary;
            creation::assets::AssetDescriptor descriptor;
            juce::String errorMessage;
            
            if (creation::assets::ProjectAssetService::importFile(*projectSession, sourceFile, options, descriptor, errorMessage))
            {
                std::string error;
                auto* node = ce::node_system::AddRegisteredNode(graph, registry.TypeRegistry(), creation_texture::nodes::NodeType::ImageInput, &error);
                if (node)
                {
                    node->SetEditorPosition(world.x, currentY);
                    
                    for (const auto& inputPin : node->Inputs())
                    {
                        if (inputPin.name == creation_texture::nodes::PinName::AssetPath)
                        {
                            if (auto* mutablePin = node->FindPin(inputPin.id))
                                mutablePin->defaultValue = descriptor.logicalPath.toStdString();
                            break;
                        }
                    }
                    currentY += 100.0f;
                }
            }
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

void NodeGraphPanel::handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds)
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
#include <node_system/frgraph_serialization.h>

void NodeGraphPanel::saveGraph(const juce::File& file)
{
    auto text = ce::node_system::SerializeGraph(graph);
    file.replaceWithText(text);
}

void NodeGraphPanel::loadGraph(const juce::File& file)
{
    if (!file.existsAsFile()) return;
    
    std::string errStr;
    auto newGraph = ce::node_system::DeserializeGraph(file.loadFileAsString().toStdString(), errStr);
    
    if (newGraph)
    {
        graph = std::move(*newGraph);
        graphComponent.GraphReplaced();
        graphComponent.repaint();
    }
    else
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Load Error", "Failed to load graph:\n" + juce::String(errStr));
    }
}






