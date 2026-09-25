#include <creation/assets/ProjectAssetService.h>
#include "NodeGraphPanel.h"
#include "node_system/frgraph_serialization.h"
#include "ViewerNodeEditor.h"
#include "ContrastAdjustmentEditor.h"

namespace {
    ce::node_system::NodeTypeRegistry BuildMaterialRegistry() {
        ce::node_system::NodeTypeRegistry reg;
        ce::material::RegisterMaterialNodes(reg);
        return reg;
    }
}

NodeGraphPanel::NodeGraphPanel()
    : registry(BuildMaterialRegistry()),
      graph("Material", ce::node_system::GraphTarget::Material),
      graphComponent(graph, registry),
      paletteComponent(registry)
{
    addAndMakeVisible(paletteComponent);
    addAndMakeVisible(graphComponent);

    graphComponent.onGraphChanged = [this]() {
        if (onGraphEdited) onGraphEdited();
    };

    graphComponent.onGetNodeExtraHeight = [this](ce::node_system::NodeId id) {
        if (auto* node = graph.FindNode(id)) {
            if (node->TypeName() == "material.texture.sample2d") return 120.0f;
        }
        return 0.0f;
    };

    graphComponent.onPaintNode = [this](juce::Graphics& g, ce::node_system::NodeId id, juce::Rectangle<float> bounds) {
        if (auto* node = graph.FindNode(id)) {
            if (node->TypeName() == "material.texture.sample2d") {
                std::string assetPath;
                for (const auto& pin : node->Inputs()) {
                    if (pin.name == "texture") {
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
    

    clearGraph();
}

void NodeGraphPanel::addViewer(ViewerNodeEditor* v) {
    activeViewers.add(v);
    // The graph compiled before this viewer was attached; show that result now, not only after the next compile.
    if (currentSnapshot != nullptr)
        v->setSnapshot(currentSnapshot);
    v->onCompileRequested = [this]() { compileGraph(); };
    v->onSaveRequested = [this]() {
        if (onSaveRequested) onSaveRequested();
    };
}

NodeGraphPanel::~NodeGraphPanel() = default;

void NodeGraphPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1e2227)); // Background
}

void NodeGraphPanel::resized()
{
    auto bounds = getLocalBounds();
    paletteComponent.setBounds(bounds.removeFromLeft(200));
    graphComponent.setBounds(bounds);
}

void NodeGraphPanel::handleNodeDoubleClicked(ce::node_system::NodeId id, juce::Rectangle<float> bounds)
{
    auto* node = graph.FindNode(id);
    if (!node) return;

    if (node->TypeName() == "material.texture.sample2d")
    {
        if (!projectSession || !projectSession->isValid()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Texture Picker", "No active project.");
            return;
        }
        auto patchAssets = projectSession->getManifest().assetCatalog.query({ creation::assets::AssetKind::texture });
        juce::PopupMenu menu;
        menu.addSectionHeader("Load Texture");
        if (patchAssets.isEmpty()) {
            menu.addItem(1, "No textures found in project", false);
        } else {
            int itemId = 100;
            std::vector<creation::assets::AssetDescriptor> assets;
            for (const auto& a : patchAssets) {
                menu.addItem(itemId++, a.displayName);
                assets.push_back(a);
            }
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [this, id, assets](int result) {
                    if (result >= 100) {
                        int index = result - 100;
                        if (index < assets.size()) {
                            if (auto* n = graph.FindNode(id)) {
                                for (auto& pin : n->Inputs()) {
                                    if (pin.name == "texture") {
                                        if (auto* mutablePin = n->FindPin(pin.id))
                                            mutablePin->defaultValue = assets[index].logicalPath.toStdString();
                                        break;
                                    }
                                }
                                graphComponent.repaint();
                            }
                        }
                    }
                });
        }
    }
}

bool NodeGraphPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files) {
        juce::File file(f);
        if (file.hasFileExtension("png") || file.hasFileExtension("jpg") || file.hasFileExtension("jpeg"))
            return true;
    }
    return false;
}

void NodeGraphPanel::compileGraph() {
    ce::material::MaterialCompileResult res = ce::material::CompileMaterialGraph(graph, registry);
    if (!res.ok) {
        juce::String errStr;
        for (const auto& err : res.errors) errStr += juce::String(err) + "\n";
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Compile Error", errStr);
    } else {
        currentSnapshot = std::make_shared<TextureFrameSnapshot>();
        currentSnapshot->generatedGlsl = res.source.declarations + "\n" + res.source.evaluateFunction;
        for (size_t i = 0; i < res.source.textures.size(); ++i) {
            const auto& tex = res.source.textures[i];
            TextureFrameImageSlot slot;
            slot.uniformName = tex.uniformName;
            if (projectSession && projectSession->isValid()) {
                juce::MemoryBlock block;
                if (projectSession->readEntry(tex.path, block))
                    slot.image = juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());
            }
            currentSnapshot->imageSlots.push_back(slot);
        }
        currentSnapshot->debugColour = juce::Colour(0xff121212);
        for (auto& viewer : activeViewers) {
            if (viewer != nullptr) viewer->setSnapshot(currentSnapshot);
        }
    }
}

void NodeGraphPanel::fileDragEnter(const juce::StringArray& files, int x, int y) {}
void NodeGraphPanel::fileDragMove(const juce::StringArray& files, int x, int y) {}
void NodeGraphPanel::fileDragExit(const juce::StringArray& files) {}

void NodeGraphPanel::filesDropped(const juce::StringArray& files, int x, int y) {}

class EditorDocumentWindow : public juce::DocumentWindow {
public:
    EditorDocumentWindow(const juce::String& name, juce::Component* content)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton)
    {
        setUsingNativeTitleBar(true);
        setContentOwned(content, true);
        setResizable(true, true);
        centreWithSize(800, 600);
        setVisible(true);
    }
    void closeButtonPressed() override { delete this; }
};


juce::String NodeGraphPanel::getGraphText() const
{
    return juce::String(ce::node_system::SerializeGraph(graph));
}

bool NodeGraphPanel::loadGraphText(const juce::String& frgraphText, juce::String& errorMessage)
{
    std::string parseError;
    auto loaded = ce::node_system::DeserializeGraph(frgraphText.toStdString(), parseError);
    if (loaded == nullptr)
    {
        errorMessage = "This material could not be read: " + juce::String(parseError);
        return false;
    }

    graph = std::move(*loaded);
    graph.SetTarget(ce::node_system::GraphTarget::Material);
    imagePreviewCache.clear();
    graphComponent.GraphReplaced();
    compileGraph();
    return true;
}

void NodeGraphPanel::clearGraph()
{
    // A new material starts with its Material Output node in place - a graph without one does not compile.
    graph = ce::node_system::Graph("Material", ce::node_system::GraphTarget::Material);
    if (auto* output = ce::node_system::AddRegisteredNode(graph, registry, "material.surface.output"))
        output->SetEditorPosition(400.0f, 150.0f);
    imagePreviewCache.clear();
    graphComponent.GraphReplaced();
    compileGraph();
}


