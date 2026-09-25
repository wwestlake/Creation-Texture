#include <creation/assets/ProjectAssetService.h>
#include "NodeGraphPanel.h"
#include "node_system/frgraph_serialization.h"
#include "ViewerNodeEditor.h"
#include "ContrastAdjustmentEditor.h"
#include "TextureSampleNodePanel.h"

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
                if (!assetPath.empty()) {
                    auto img = getProjectImage(juce::String(assetPath));
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
    if (node == nullptr || node->TypeName() != "material.texture.sample2d")
        return;

    juce::String currentPath;
    for (const auto& pin : node->Inputs())
        if (pin.name == "texture" && std::holds_alternative<std::string>(pin.defaultValue))
            currentPath = juce::String(std::get<std::string>(pin.defaultValue));

    juce::Array<TextureSampleNodePanel::ImageChoice> choices;
    if (projectSession != nullptr && projectSession->isValid())
    {
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
        {
            if (! isImageAsset(asset))
                continue;

            TextureSampleNodePanel::ImageChoice choice;
            choice.displayName = asset.displayName.isNotEmpty() ? asset.displayName
                                                                : asset.logicalPath.fromLastOccurrenceOf("/", false, false);
            choice.logicalPath = asset.logicalPath;
            const auto image = getProjectImage(asset.logicalPath);
            if (image.isValid())
            {
                choice.thumbnail = image.rescaled(juce::jmin(96, image.getWidth()), juce::jmin(96, image.getHeight()),
                                                  juce::Graphics::mediumResamplingQuality);
                choice.details = asset.logicalPath.fromLastOccurrenceOf(".", false, false).toUpperCase()
                               + "  " + juce::String(image.getWidth()) + " x " + juce::String(image.getHeight());
            }
            else
            {
                choice.details = "Could not be read";
            }
            choices.add(choice);
        }
    }

    auto panel = std::make_unique<TextureSampleNodePanel>(choices, currentPath, [this, id](const juce::String& logicalPath) {
        auto* sampleNode = graph.FindNode(id);
        if (sampleNode == nullptr)
            return;

        for (const auto& pin : sampleNode->Inputs())
        {
            if (pin.name == "texture")
            {
                if (auto* mutablePin = sampleNode->FindPin(pin.id))
                    mutablePin->defaultValue = logicalPath.toStdString();
                break;
            }
        }

        graphComponent.repaint();
        compileGraph();
        if (onGraphEdited)
            onGraphEdited();
    });

    // Open beside the node that was double-clicked - the panel belongs to that node.
    const auto nodeArea = graphComponent.localAreaToGlobal(bounds.toNearestInt());
    juce::CallOutBox::launchAsynchronously(std::move(panel), nodeArea, nullptr);
}

bool NodeGraphPanel::isImageAsset(const creation::assets::AssetDescriptor& asset)
{
    // Only formats this app can decode. Recognised by what the file is, not by an asset-kind label.
    static const juce::StringArray imageExtensions { "png", "jpg", "jpeg", "gif" };
    const auto extension = asset.logicalPath.fromLastOccurrenceOf(".", false, false).toLowerCase();
    return imageExtensions.contains(extension);
}

juce::Image NodeGraphPanel::getProjectImage(const juce::String& logicalPath)
{
    const auto key = logicalPath.toStdString();
    auto cached = imagePreviewCache.find(key);
    if (cached != imagePreviewCache.end())
        return cached->second;

    juce::Image image;
    juce::MemoryBlock block;
    if (projectSession != nullptr && projectSession->isValid() && projectSession->readEntry(logicalPath, block))
        image = juce::ImageFileFormat::loadFrom(block.getData(), block.getSize());

    imagePreviewCache[key] = image;
    return image;
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


