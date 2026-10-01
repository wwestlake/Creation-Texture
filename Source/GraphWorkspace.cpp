#include "GraphWorkspace.h"

#include <TextureSet.h>
#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>
#include <node_system/frgraph_serialization.h>
#include <creation/material/material_compiler.h>

namespace ns = ce::node_system;

namespace
{
constexpr const char* documentFormat = "djehuti-image-graph";
constexpr float thumbnailHeight = 100.0f;
const juce::Colour panelBackground { 0xff1e2227 };

// A small display thumbnail straight from the float image (nearest sample), without converting the whole image.
// A Drawing's thumbnail: its paths as thin lines on a dark square (the canvas, 0..1 both ways).
juce::Image drawingThumbnail(const drawing::Drawing& d, int size)
{
    juce::Image image(juce::Image::ARGB, size, size, true);
    juce::Graphics g(image);
    g.fillAll(juce::Colour(0xff101318));
    g.setColour(juce::Colour(0xffe8edf2));
    const float s = static_cast<float>(size);
    size_t drawn = 0;
    for (const auto& path : d.paths)
    {
        if (path.points.empty() || drawn > 200000)
            continue;
        juce::Path p;
        p.startNewSubPath(path.points[0].x * s, path.points[0].y * s);
        for (size_t i = 1; i < path.points.size(); ++i)
            p.lineTo(path.points[i].x * s, path.points[i].y * s);
        if (path.closed)
            p.closeSubPath();
        g.strokePath(p, juce::PathStrokeType(1.0f));
        drawn += path.points.size();
    }
    return image;
}

juce::Image thumbnailOf(const image_graph::Image& image, int size)
{
    if (image.width <= 0 || image.height <= 0)
        return {};
    const float scale = juce::jmin(1.0f, static_cast<float>(size) / static_cast<float>(juce::jmax(image.width, image.height)));
    const int w = juce::jmax(1, juce::roundToInt(static_cast<float>(image.width) * scale));
    const int h = juce::jmax(1, juce::roundToInt(static_cast<float>(image.height) * scale));
    image_graph::Image small;
    small.width = w;
    small.height = h;
    small.data = image.data;
    small.rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            const int sx = juce::jmin(image.width - 1, x * image.width / w);
            const int sy = juce::jmin(image.height - 1, y * image.height / h);
            const size_t from = (static_cast<size_t>(sy) * static_cast<size_t>(image.width) + static_cast<size_t>(sx)) * 4;
            const size_t to = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4;
            for (int c = 0; c < 4; ++c)
                small.rgba[to + static_cast<size_t>(c)] = image.rgba[from + static_cast<size_t>(c)];
        }
    return image_graph::toDisplayImage(small);
}

void drawChecker(juce::Graphics& g, juce::Rectangle<int> area, int cell)
{
    g.saveState();
    g.reduceClipRegion(area);
    for (int y = area.getY(); y < area.getBottom(); y += cell)
        for (int x = area.getX(); x < area.getRight(); x += cell)
        {
            g.setColour((((x - area.getX()) / cell + (y - area.getY()) / cell) % 2) == 0 ? juce::Colour(0xff3a3a3a) : juce::Colour(0xff555555));
            g.fillRect(x, y, cell, cell);
        }
    g.restoreState();
}

juce::String slugFor(const juce::String& name)
{
    return texture_set::slugFor(name);
}
}

//==============================================================================
// Evaluates on a background thread from a copy of the graph (serialised text - node ids round-trip exactly).
// Only the latest request matters.
class GraphWorkspace::Worker final : public juce::Thread, public juce::ChangeBroadcaster
{
public:
    struct Result
    {
        std::map<ns::NodeId, juce::Image> thumbnails;
        std::map<ns::NodeId, juce::String> errors;
        ns::NodeId previewNode = 0;
        std::string previewOutput;
        image_graph::ImagePtr previewImage;
        juce::Image previewDisplay;
        juce::String previewError;
        ns::NodeId overlayNode = 0;
        drawing::DrawingPtr overlay;
    };

    Worker(const image_graph::Library& lib, std::function<creation::assets::ProjectSession*()> session)
        : juce::Thread("Image Graph"), library(lib), projectSession(std::move(session))
    {
        startThread();
    }

    ~Worker() override { stopThread(4000); }

    void request(std::string graphText, ns::NodeId previewNode, std::string previewOutput, ns::NodeId overlayNode)
    {
        {
            const juce::ScopedLock lock(requestLock);
            pending = { std::move(graphText), previewNode, std::move(previewOutput), overlayNode, true };
        }
        notify();
    }

    bool takeResult(Result& out)
    {
        const juce::ScopedLock lock(resultLock);
        if (! hasResult)
            return false;
        out = std::move(result);
        hasResult = false;
        return true;
    }

    std::atomic<bool> busy { false };

    void run() override
    {
        while (! threadShouldExit())
        {
            Request job;
            {
                const juce::ScopedLock lock(requestLock);
                if (pending.valid)
                {
                    job = std::move(pending);
                    pending = {};
                }
            }
            if (! job.valid)
            {
                wait(-1);
                continue;
            }

            busy = true;
            sendChangeMessage();
            if (evaluator == nullptr)
            {
                image_graph::Host host;
                host.loadImage = [this](const juce::String& path) { return loadProjectImage(path); };
                host.loadGraph = [this](const juce::String& path) {
                    juce::MemoryBlock bytes;
                    auto* session = projectSession();
                    if (session == nullptr || ! session->isValid() || ! session->readEntry(path, bytes))
                        return image_graph::Host::LoadedGraph {};
                    return image_graph::readGraphDocument(bytes.toString());
                };
                evaluator = std::make_unique<image_graph::Evaluator>(library, host);
            }

            Result out;
            std::string parseError;
            auto copy = ns::DeserializeGraph(job.graphText, parseError);
            if (copy != nullptr)
            {
                for (const auto& [id, node] : copy->Nodes())
                {
                    if (threadShouldExit() || pendingArrived())
                        break;
                    if (node->Outputs().empty())
                        continue;
                    juce::String error;
                    if (node->Outputs().front().type.dataType == ns::DataType::Drawing)
                    {
                        if (auto shapes = evaluator->evaluateDrawing(*copy, id, node->Outputs().front().name, error))
                            out.thumbnails[id] = drawingThumbnail(*shapes, 96);
                        else
                            out.errors[id] = error;
                        continue;
                    }
                    if (node->Outputs().front().type.dataType != ns::DataType::Texture)
                        continue; // value and brush nodes have no picture
                    if (ns::FlowKindOf(*node) == ns::FlowKind::route)
                    {
                        // A Route shows what flows out of its chosen output.
                        const auto chosen = evaluator->flowChosenCase(*copy, id, error);
                        if (auto image = chosen.empty() ? nullptr : evaluator->evaluate(*copy, id, chosen, error))
                            out.thumbnails[id] = thumbnailOf(*image, 96);
                        else
                            out.errors[id] = error;
                        continue;
                    }
                    if (auto image = evaluator->evaluate(*copy, id, node->Outputs().front().name, error))
                        out.thumbnails[id] = thumbnailOf(*image, 96);
                    else
                        out.errors[id] = error;
                }
                if (job.previewNode != 0 && copy->FindNode(job.previewNode) != nullptr)
                {
                    out.previewNode = job.previewNode;
                    out.previewOutput = job.previewOutput;
                    out.previewImage = evaluator->evaluate(*copy, job.previewNode, job.previewOutput, out.previewError);
                    if (out.previewImage != nullptr)
                        out.previewDisplay = image_graph::toDisplayImage(*out.previewImage);
                }
                if (const auto* overlayNode = copy->FindNode(job.overlayNode))
                    for (const auto& pin : overlayNode->Outputs())
                        if (pin.type.dataType == ns::DataType::Drawing)
                        {
                            juce::String overlayError;
                            out.overlayNode = job.overlayNode;
                            out.overlay = evaluator->evaluateDrawing(*copy, job.overlayNode, pin.name, overlayError);
                            break;
                        }
            }

            {
                const juce::ScopedLock lock(resultLock);
                result = std::move(out);
                hasResult = true;
            }
            busy = false;
            sendChangeMessage();
        }
    }

private:
    struct Request
    {
        std::string graphText;
        ns::NodeId previewNode = 0;
        std::string previewOutput;
        ns::NodeId overlayNode = 0;
        bool valid = false;
    };

    bool pendingArrived()
    {
        const juce::ScopedLock lock(requestLock);
        return pending.valid;
    }

    // Project images, read on this thread and kept (decoding is the slow part).
    image_graph::ImagePtr loadProjectImage(const juce::String& path)
    {
        auto found = loaded.find(path);
        if (found != loaded.end())
            return found->second;
        image_graph::ImagePtr image;
        auto* session = projectSession();
        juce::MemoryBlock bytes;
        if (session != nullptr && session->isValid() && session->readEntry(path, bytes))
            image = image_graph::fromDisplayImage(juce::ImageFileFormat::loadFrom(bytes.getData(), bytes.getSize()));
        loaded[path] = image;
        return image;
    }

    const image_graph::Library& library;
    std::function<creation::assets::ProjectSession*()> projectSession;
    std::unique_ptr<image_graph::Evaluator> evaluator; // created on this thread (it compiles FRust)
    std::map<juce::String, image_graph::ImagePtr> loaded;
    juce::CriticalSection requestLock, resultLock;
    Request pending;
    Result result;
    bool hasResult = false;
};

//==============================================================================
// The chosen node's chosen output, large: fit to the panel over a checkerboard. Pin keeps showing that node while
// other nodes are selected.
class GraphWorkspace::PreviewPanel final : public juce::Component
{
public:
    std::function<void()> onTargetChanged;

    PreviewPanel()
    {
        outputBox.onChange = [this]() { if (onTargetChanged) onTargetChanged(); };
        addAndMakeVisible(outputBox);
        pin.setClickingTogglesState(true);
        pin.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2f5d8a));
        pin.setTooltip("Keep showing this node while you select others");
        addAndMakeVisible(pin);
        info.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(info);
    }

    bool isPinned() const { return pin.getToggleState(); }
    ns::NodeId getNode() const { return node; }
    std::string getOutput() const { return outputBox.getText().toStdString(); }

    void showNode(ns::NodeId id, const juce::String& nodeName, const std::vector<std::string>& outputs)
    {
        node = id;
        name = nodeName;
        const auto previous = outputBox.getText();
        outputBox.clear(juce::dontSendNotification);
        int selectId = 1;
        for (size_t i = 0; i < outputs.size(); ++i)
        {
            outputBox.addItem(outputs[i], static_cast<int>(i) + 1);
            if (juce::String(outputs[i]) == previous)
                selectId = static_cast<int>(i) + 1;
        }
        if (! outputs.empty())
            outputBox.setSelectedId(selectId, juce::dontSendNotification);
        image = {};
        repaint();
    }

    void setImage(const juce::Image& display, const juce::String& text)
    {
        image = display;
        info.setText(text, juce::dontSendNotification);
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111317));
        auto area = getLocalBounds().withTrimmedTop(34).withTrimmedBottom(22).reduced(8);
        if (image.isValid())
        {
            const auto fitted = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                    .appliedTo(image.getBounds().toFloat(), area.toFloat()).getSmallestIntegerContainer();
            drawChecker(g, fitted, 10);
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
            g.drawImage(image, fitted.toFloat());
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.drawFittedText(node == 0 ? "Select a node to preview it." : "Computing...", area, juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop(34).reduced(4);
        pin.setBounds(top.removeFromRight(60));
        top.removeFromRight(6);
        outputBox.setBounds(top.removeFromLeft(juce::jmin(200, top.getWidth())));
        info.setBounds(getLocalBounds().removeFromBottom(22).reduced(8, 0));
    }

private:
    juce::ComboBox outputBox;
    juce::TextButton pin { "Pin" };
    juce::Label info;
    juce::Image image;
    ns::NodeId node = 0;
    juce::String name;
};

//==============================================================================
namespace
{
ns::NodeTypeRegistry makeRegistry(const image_graph::Library& library)
{
    // One registry for every kind of graph; each graph's type picks the nodes that belong in it (GRAPH_TYPES.md).
    ns::NodeTypeRegistry registry;
    library.registerTypes(registry);
    ce::material::RegisterMaterialNodes(registry);
    // Number and colour decisions belong in materials too (a shader select); the rest only in image graphs.
    ns::RegisterFlowNodes(registry, { ns::StandardFlowType(ns::DataType::Float), ns::StandardFlowType(ns::DataType::Color) },
                          { image_graph::kImageDiagram, ce::material::kMaterialDiagram });
    return registry;
}
}

GraphWorkspace::GraphWorkspace()
    : registry(makeRegistry(library)),
      palette(registry),
      preview(std::make_unique<PreviewPanel>()),
      worker(std::make_unique<Worker>(library, [this]() { return projectSession; }))
{
    // An image graph: the palette lists the nodes that belong in one (shared/NodeSystem/GRAPH_TYPES.md).
    graph.SetDiagramType(image_graph::kImageDiagram);
    palette.SetDiagramType(image_graph::kImageDiagram);

    graphView.onGraphChanged = [this]() {
        properties.refresh(); // wiring changed: which inputs are editable changed
        graphEdited();
    };
    graphView.onSelectionChanged = [this](ns::NodeId id) { selectionChanged(id); };
    graphView.onGetNodeExtraHeight = [this](ns::NodeId id) {
        const auto* node = graph.FindNode(id);
        if (node == nullptr || node->Outputs().empty())
            return 0.0f;
        if (node->TypeName() == "material.texture.sample2d")
            return thumbnailHeight; // shows the image it samples
        const auto type = node->Outputs().front().type.dataType;
        return type == ns::DataType::Texture || type == ns::DataType::Drawing ? thumbnailHeight : 0.0f;
    };
    graphView.onPaintNode = [this](juce::Graphics& g, ns::NodeId id, juce::Rectangle<float> bounds) {
        // The thumbnail sits below the pin rows; the bounds are on screen, so scale its height by the zoom.
        const float zoom = graphView.Zoom();
        auto area = bounds.withTrimmedTop(bounds.getHeight() - thumbnailHeight * zoom).reduced(8.0f * zoom, 6.0f * zoom);
        auto error = errors.find(id);
        if (error != errors.end())
        {
            // A path the decision did not take is not an error: grey, not red.
            g.setColour(error->second.contains("Not chosen") ? juce::Colour(0xff8a94a3) : juce::Colour(0xffff8a80));
            g.setFont(juce::FontOptions(11.0f));
            g.drawFittedText(error->second, area.toNearestInt(), juce::Justification::centred, 4);
            return;
        }
        auto thumb = thumbnails.find(id);
        if (thumb == thumbnails.end() && images.thumbnail)
            if (const auto* node = graph.FindNode(id); node != nullptr && node->TypeName() == "material.texture.sample2d")
                for (const auto& pin : node->Inputs())
                    if (const auto* path = std::get_if<std::string>(&pin.defaultValue); pin.name == "texture" && path != nullptr && ! path->empty())
                        thumb = thumbnails.emplace(id, images.thumbnail(juce::String(*path))).first;
        if (thumb != thumbnails.end() && thumb->second.isValid())
        {
            const auto fitted = juce::RectanglePlacement(juce::RectanglePlacement::centred)
                                    .appliedTo(thumb->second.getBounds().toFloat(), area).getSmallestIntegerContainer();
            drawChecker(g, fitted, 6);
            g.drawImage(thumb->second, fitted.toFloat());
        }
    };

    NodePropertiesPanel::Host host;
    host.graph = &graph;
    host.registry = &registry;
    host.onValueEdited = [this]() { graphEdited(); };
    host.customEditor = [this](ns::Node& node, const ns::Pin& pin, int& height) { return customEditor(node, pin, height); };
    properties.setHost(std::move(host));

    preview->onTargetChanged = [this]() { requestEvaluation(); };

    // A material graph recompiles on edits; its preview can ask for it, and save.
    materialPreview.getViewer()->onCompileRequested = [this]() { compileMaterial(); };
    materialPreview.getViewer()->onSaveRequested = [this]() { saveGraph(); };

    // Params, constants and variables (shared/NodeSystem/SYMBOLS.md): drag one onto the graph for a Get node.
    // The Draw view's script editor writes into the selected Draw Script node.
    scriptPanel.onScriptChanged = [this](const juce::String& text) {
        if (auto* node = graph.FindNode(scriptNode))
            for (const auto& pin : node->Inputs())
                if (pin.name == "script")
                {
                    node->FindPin(pin.id)->defaultValue = text.toStdString();
                    graphEdited();
                }
    };

    symbols.setEnums(registry); // each enum is a Choice type in the Variables panel
    symbols.onSymbolsChanged = [this]() {
        graphView.repaint();
        properties.refresh();
        graphEdited();
    };
    worker->addChangeListener(this);
}

GraphWorkspace::~GraphWorkspace()
{
    worker->removeChangeListener(this);
    worker.reset();
}

juce::Component& GraphWorkspace::getPreview() noexcept { return *preview; }

void GraphWorkspace::setProjectSession(creation::assets::ProjectSession* session)
{
    projectSession = session;
}

void GraphWorkspace::setImageSource(project_images::Source source)
{
    images = std::move(source);
    auto host = NodePropertiesPanel::Host {};
    host.graph = &graph;
    host.registry = &registry;
    host.projectImages = images;
    host.onValueEdited = [this]() { graphEdited(); };
    host.customEditor = [this](ns::Node& node, const ns::Pin& pin, int& height) { return customEditor(node, pin, height); };
    properties.setHost(std::move(host));
}

void GraphWorkspace::status(const juce::String& text)
{
    if (onStatus)
        onStatus(text);
}

juce::String GraphWorkspace::getTitle() const
{
    return (graphName.isNotEmpty() ? graphName : juce::String(isMaterial() ? "Untitled material" : "Untitled image graph")) + (edited ? " *" : "");
}

void GraphWorkspace::graphEdited()
{
    edited = true;
    // Decisions name their cases after what drives the selector (FLOW.md): keep them in line with the wiring.
    bool casesChanged = false;
    for (const auto& [id, node] : graph.Nodes())
        if (ns::FlowKindOf(*node) != ns::FlowKind::none)
            casesChanged = ns::SyncFlowNodeCases(graph, registry, id) || casesChanged;
    // Properties shows the new case pins - rebuilt after this edit returns, never during it: the edit can come from a
    // control in Properties itself (the cases count), and rebuilding then deletes that control while it is running.
    if (casesChanged)
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<NodePropertiesPanel>(&properties)]() {
            if (safe != nullptr)
                safe->refresh();
        });
    graphView.repaint();
    symbols.graphChanged(); // a Get node may have been added, removed or rebound
    // The Draw view follows: a deleted node leaves it; a script edited in Properties shows in the Script panel.
    if (graph.FindNode(overlayNode) == nullptr)
    {
        overlayNode = 0;
        drawCanvas.setOverlay(nullptr, {});
    }
    if (graph.FindNode(scriptNode) == nullptr)
    {
        scriptNode = 0;
        scriptPanel.showScript({}, {});
    }
    else
        scriptPanel.scriptChangedElsewhere(juce::String(scriptText(scriptNode)));
    requestEvaluation();
}

std::string GraphWorkspace::scriptText(ns::NodeId id) const
{
    if (const auto* node = graph.FindNode(id))
        for (const auto& pin : node->Inputs())
            if (pin.name == "script")
                if (const auto* text = std::get_if<std::string>(&pin.defaultValue))
                    return *text;
    return {};
}

void GraphWorkspace::forgetDrawTargets()
{
    overlayNode = 0;
    scriptNode = 0;
    drawCanvas.setOverlay(nullptr, {});
    drawCanvas.setImage({}, {});
    scriptPanel.showScript({}, {});
}

void GraphWorkspace::selectionChanged(ns::NodeId id)
{
    selectedNode = id;
    properties.showNode(id);

    // Draw view: a drawing node shows its lines over the canvas; a Paint node shows the drawing wired into it.
    // A Draw Script node goes into the Script panel. Other nodes leave both as they were.
    if (const auto* node = graph.FindNode(id))
    {
        const auto* descriptor = registry.Find(node->TypeName());
        const auto name = descriptor != nullptr ? juce::String(descriptor->displayName) : juce::String(node->TypeName());
        auto showOverlay = [this](ns::NodeId drawingNode, const juce::String& label) {
            overlayNode = drawingNode;
            overlayLabel = label;
            drawCanvas.setOverlay(nullptr, label);
        };
        if (! node->Outputs().empty() && node->Outputs().front().type.dataType == ns::DataType::Drawing)
            showOverlay(id, "Lines: " + name);
        else if (node->TypeName() == "draw.paint")
            for (const auto& wire : graph.Connections())
                if (wire.toNode == id)
                    if (const auto* to = node->FindPin(wire.toPin); to != nullptr && to->name == "drawing")
                        if (const auto* from = graph.FindNode(wire.fromNode))
                        {
                            const auto* fromType = registry.Find(from->TypeName());
                            showOverlay(wire.fromNode, "Lines: " + (fromType != nullptr ? juce::String(fromType->displayName) : juce::String(from->TypeName())));
                        }
        if (node->TypeName() == "draw.script")
        {
            scriptNode = id;
            scriptPanel.showScript(name, juce::String(scriptText(id)));
            const auto error = errors.find(id);
            scriptPanel.setError(error != errors.end() ? error->second : juce::String());
        }
    }

    if (preview->isPinned() || id == 0)
    {
        requestEvaluation();
        return;
    }
    const auto* node = graph.FindNode(id);
    if (node == nullptr)
        return;
    std::vector<std::string> outputs;
    for (const auto& pin : node->Outputs())
        if (pin.type.dataType == ns::DataType::Texture)
            outputs.push_back(pin.name);
    if (outputs.empty())
    {
        requestEvaluation(); // a Get, value or drawing node has no picture: the preview stays on the last image
        return;
    }
    const auto* descriptor = registry.Find(node->TypeName());
    preview->showNode(id, descriptor != nullptr ? juce::String(descriptor->displayName) : juce::String(node->TypeName()), outputs);
    requestEvaluation();
}

void GraphWorkspace::requestEvaluation()
{
    if (isMaterial())
    {
        compileMaterial(); // a material is compiled, not evaluated
        return;
    }
    worker->request(ns::SerializeGraph(graph), preview->getNode(), preview->getOutput(), overlayNode);
}

void GraphWorkspace::changeListenerCallback(juce::ChangeBroadcaster*)
{
    Worker::Result result;
    if (! worker->takeResult(result))
        return;

    thumbnails = std::move(result.thumbnails);
    errors = std::move(result.errors);
    graphView.repaint();

    if (result.previewNode != 0 && result.previewNode == preview->getNode())
    {
        if (result.previewImage != nullptr)
        {
            const auto text = juce::String(result.previewOutput) + "  " + juce::String(result.previewImage->width) + " x "
                            + juce::String(result.previewImage->height) + (result.previewImage->data ? "  (data)" : "");
            preview->setImage(result.previewDisplay, text);
            drawCanvas.setImage(result.previewDisplay, text);
        }
        else
        {
            preview->setImage({}, result.previewError);
            drawCanvas.setImage({}, result.previewError);
        }
        lastPreview = result.previewImage;
    }
    if (result.overlayNode != 0 && result.overlayNode == overlayNode)
        drawCanvas.setOverlay(result.overlay, overlayLabel);
    if (scriptNode != 0)
    {
        const auto error = errors.find(scriptNode);
        scriptPanel.setError(error != errors.end() ? error->second : juce::String());
    }
}

// A material graph becomes a shader for the 3D Preview; its texture samples read the project's images.
void GraphWorkspace::compileMaterial()
{
    auto* viewer = materialPreview.getViewer();
    const auto result = ce::material::CompileMaterialGraph(graph, registry);
    if (! result.ok)
    {
        juce::StringArray problems;
        for (const auto& error : result.errors)
            problems.add(juce::String(error));
        viewer->setGraphProblem("The material cannot be previewed yet: " + problems.joinIntoString(" "));
        return;
    }
    viewer->setGraphProblem({});
    auto snapshot = std::make_shared<TextureFrameSnapshot>();
    snapshot->generatedGlsl = result.source.declarations + "\n" + result.source.evaluateFunction;
    for (const auto& texture : result.source.textures)
    {
        TextureFrameImageSlot slot;
        slot.uniformName = texture.uniformName;
        if (images.image)
            slot.image = images.image(juce::String(texture.path));
        snapshot->imageSlots.push_back(slot);
    }
    snapshot->debugColour = juce::Colour(0xff121212);
    viewer->setSnapshot(snapshot);
}

// Node-specific editors in Properties.
std::unique_ptr<juce::Component> GraphWorkspace::customEditor(ns::Node& node, const ns::Pin& pin, int& height)
{
    const auto type = node.TypeName();

    // A Get node: choose which param / constant / variable it reads, from those of its type.
    if (ns::IsSymbolGetNode(type) && pin.name == ns::kSymbolIdPin)
    {
        auto box = std::make_unique<juce::ComboBox>();
        const auto current = std::holds_alternative<std::string>(pin.defaultValue) ? std::get<std::string>(pin.defaultValue) : std::string();
        // Only symbols whose value still fits every setting this Get node is wired into (a Choice's enum).
        std::vector<ns::PinTypeDesc> wiredInto;
        for (const auto& wire : graph.Connections())
            if (wire.fromNode == node.Id())
                if (const auto* to = graph.FindNode(wire.toNode))
                    if (const auto* toPin = to->FindPin(wire.toPin))
                    {
                        auto t = toPin->type;
                        if (const auto* def = ns::PinEnum(registry, *to, *toPin))
                            t.enumType = def->name;
                        wiredInto.push_back(t);
                    }
        auto fits = [&wiredInto, &node](const ns::Symbol& symbol) {
            auto out = node.Outputs().front().type;
            out.enumType = symbol.enumType;
            for (const auto& t : wiredInto)
                if (! ns::IsConnectionCompatible(out, t))
                    return false;
            return true;
        };
        int itemId = 1;
        std::vector<std::string> ids;
        for (const auto& symbol : graph.Symbols())
            if (ns::SymbolGetNodeType(symbol.type) == type && fits(symbol))
            {
                box->addItem(juce::String(symbol.name) + "  (" + creation::node_editor_ui::symbolKindName(symbol.kind) + ")", itemId);
                if (symbol.id == current)
                    box->setSelectedId(itemId, juce::dontSendNotification);
                ids.push_back(symbol.id);
                ++itemId;
            }
        box->setTextWhenNothingSelected(ids.empty() ? "No symbols of this type - add one in Variables" : "Choose a symbol");
        box->onChange = [this, nodeId = node.Id(), ids, b = box.get()]() {
            const int index = b->getSelectedId() - 1;
            auto* target = graph.FindNode(nodeId);
            if (target == nullptr || ! juce::isPositiveAndBelow(index, static_cast<int>(ids.size())))
                return;
            if (const auto* symbol = graph.FindSymbol(ids[static_cast<size_t>(index)]))
                ns::BindSymbolGetNode(*target, *symbol);
            graphView.repaint();
            graphEdited();
        };
        height = 26;
        return box;
    }

    // Draw Script: a code editor, several lines high. The drawing updates as you type.
    if (type == "draw.script" && pin.name == "script")
    {
        auto editor = std::make_unique<juce::TextEditor>();
        editor->setMultiLine(true, false);
        editor->setReturnKeyStartsNewLine(true);
        editor->setTabKeyUsedAsCharacter(true);
        editor->setScrollbarsShown(true);
        editor->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
        editor->setText(std::holds_alternative<std::string>(pin.defaultValue) ? juce::String(std::get<std::string>(pin.defaultValue)) : juce::String(),
                        juce::dontSendNotification);
        editor->onTextChange = [this, nodeId = node.Id(), pinId = pin.id, e = editor.get()]() {
            if (auto* target = graph.FindNode(nodeId))
                if (auto* p = target->FindPin(pinId))
                {
                    p->defaultValue = e->getText().toStdString();
                    graphEdited();
                }
        };
        height = 260;
        return editor;
    }

    // Surface Map: load settings from a saved surface map.
    // A Switch or Route's selector, not wired: choose the case by name.
    if (ns::FlowKindOf(node) != ns::FlowKind::none && pin.name == ns::kFlowSelectorPin)
    {
        auto box = std::make_unique<juce::ComboBox>();
        const auto cases = ns::FlowCasePins(node);
        for (size_t i = 0; i < cases.size(); ++i)
            box->addItem(juce::String(cases[i]->name).replaceCharacter('_', ' '), static_cast<int>(i) + 1);
        box->setSelectedId(ns::FlowCaseIndex(pin.defaultValue, static_cast<int>(cases.size())) + 1, juce::dontSendNotification);
        box->setTooltip("Which case flows. Wire a Choice param, a toggle or a number in to decide it from outside.");
        box->onChange = [this, nodeId = node.Id(), pinId = pin.id, b = box.get()]() {
            if (auto* target = graph.FindNode(nodeId))
                if (auto* p = target->FindPin(pinId))
                {
                    p->defaultValue = static_cast<std::int64_t>(b->getSelectedId() - 1);
                    graphEdited();
                }
        };
        height = 26;
        return box;
    }

    // A Graph node: which saved image graph it uses.
    if (ns::IsGraphNode(node) && pin.name == ns::kGraphPathPin)
    {
        const auto current = std::holds_alternative<std::string>(pin.defaultValue) ? juce::String(std::get<std::string>(pin.defaultValue)) : juce::String();
        auto button = std::make_unique<juce::TextButton>(current.isNotEmpty()
                                                             ? "Uses " + current.fromLastOccurrenceOf("/", false, false).upToFirstOccurrenceOf(".imggraph", false, false)
                                                             : juce::String("Choose the graph it uses..."));
        button->setTooltip("Its params and Graph Inputs become this node's inputs, its Outputs this node's outputs.");
        button->onClick = [this, id = node.Id()]() { chooseGraphFor(id); };
        height = 28;
        return button;
    }

    if (type == "image.surfacemap" && pin.name == "surfaceMap")
    {
        const auto current = std::holds_alternative<std::string>(pin.defaultValue) ? juce::String(std::get<std::string>(pin.defaultValue)) : juce::String();
        auto button = std::make_unique<juce::TextButton>(current.isNotEmpty() ? "From " + current.fromLastOccurrenceOf("/", false, false).upToFirstOccurrenceOf(".texset", false, false)
                                                                              : juce::String("Load settings from a saved surface map..."));
        button->onClick = [this, id = node.Id()]() { chooseSurfaceMapFor(id); };
        height = 28;
        return button;
    }

    // Image inputs other than Load Image's own are wired in, not picked.
    if (! isMaterial() && pin.type.dataType == ns::DataType::Texture && type != "image.load")
    {
        auto label = std::make_unique<juce::Label>();
        const bool sizeOnly = type == "image.noise" || type == "image.cells" || type == "image.checker" || type == "image.gradient";
        label->setText(sizeOnly ? "Optional: wire an image in to take its size" : "Wire an image in", juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colours::grey);
        height = 24;
        return label;
    }
    return nullptr;
}

image_graph::Host::LoadedGraph GraphWorkspace::readProjectGraph(const juce::String& path) const
{
    juce::MemoryBlock bytes;
    if (! hasProject() || ! projectSession->readEntry(path, bytes))
        return {};
    return image_graph::readGraphDocument(bytes.toString());
}

juce::String GraphWorkspace::ownGraphPath() const
{
    return graphName.isEmpty() ? juce::String()
                               : juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(graphName) + ".imggraph.json";
}

// Every Graph node's pins follow the graph it uses as it is saved now (it may have changed since).
void GraphWorkspace::syncGraphNodes()
{
    for (const auto& [id, node] : graph.Nodes())
    {
        if (! ns::IsGraphNode(*node))
            continue;
        const auto path = ns::GraphNodePath(*node);
        if (path.empty())
            continue;
        const auto used = readProjectGraph(juce::String(path));
        if (used.graph != nullptr)
            ns::SyncGraphNodePins(graph, id, ns::InterfaceOf(*used.graph, registry));
    }
}

void GraphWorkspace::chooseGraphFor(ns::NodeId id)
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    project_images::Source graphs;
    graphs.noun = "image graph";
    graphs.list = [this]() {
        juce::Array<project_images::Entry> entries;
        const auto own = ownGraphPath();
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (asset.logicalPath.endsWith(".imggraph.json") && asset.logicalPath != own) // a graph cannot use itself
                entries.add({ asset.displayName, asset.logicalPath });
        return entries;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "The graph this node uses";
    options.content.setOwned(new ProjectImagePicker(graphs, {}, [this, id](const juce::String& path) {
        auto* node = graph.FindNode(id);
        if (node == nullptr || path.isEmpty())
            return;
        const auto used = readProjectGraph(path);
        if (used.graph == nullptr)
        {
            status("Could not read " + path + (used.error.isNotEmpty() ? ": " + used.error : juce::String()));
            return;
        }
        for (const auto& pin : node->Inputs())
            if (pin.name == ns::kGraphPathPin)
                node->FindPin(pin.id)->defaultValue = path.toStdString();
        ns::SyncGraphNodePins(graph, id, ns::InterfaceOf(*used.graph, registry));
        graphView.repaint();
        properties.refresh();
        graphEdited();
        status("This node now uses " + path.fromLastOccurrenceOf("/", false, false) + ".");
    }));
    options.componentToCentreAround = graphView.getTopLevelComponent();
    options.dialogBackgroundColour = juce::Colour(0xff161a1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void GraphWorkspace::chooseSurfaceMapFor(ns::NodeId id)
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }

    project_images::Source sets;
    sets.noun = "surface map";
    sets.list = [this]() {
        juce::Array<project_images::Entry> entries;
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (asset.logicalPath.endsWith(".texset.json"))
                entries.add({ asset.displayName, asset.logicalPath });
        return entries;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Load settings from a surface map";
    options.content.setOwned(new ProjectImagePicker(sets, {}, [this, id](const juce::String& path) {
        auto* node = graph.FindNode(id);
        juce::MemoryBlock bytes;
        if (node == nullptr || path.isEmpty() || ! projectSession->readEntry(path, bytes))
            return;
        const auto settings = juce::JSON::parse(bytes.toString())["settings"];
        for (const auto& pin : node->Inputs())
        {
            auto* target = node->FindPin(pin.id);
            if (pin.name == "surfaceMap")
                target->defaultValue = path.toStdString();
            else if (settings.hasProperty(juce::Identifier(pin.name)))
            {
                const auto value = settings[juce::Identifier(pin.name)];
                if (std::holds_alternative<bool>(pin.defaultValue))
                    target->defaultValue = static_cast<bool>(value);
                else if (std::holds_alternative<float>(pin.defaultValue))
                    target->defaultValue = static_cast<float>(static_cast<double>(value));
            }
        }
        graphEdited();
        properties.refresh();
        status("Loaded the settings of " + path.fromLastOccurrenceOf("/", false, false) + " - change any of them in Properties.");
    }));
    options.componentToCentreAround = graphView.getTopLevelComponent();
    options.dialogBackgroundColour = juce::Colour(0xff161a1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

//==============================================================================
// Documents: the graph saves as a JSON document that reopens it exactly.
void GraphWorkspace::newGraph(const std::string& diagramType)
{
    const bool material = diagramType == ce::material::kMaterialDiagram;
    ns::Graph fresh(material ? "Material" : "Image Graph", material ? ns::GraphTarget::Material : ns::GraphTarget::Dataflow);
    fresh.SetDiagramType(diagramType);
    // A new material starts with its Material Output in place - a material without one does not compile.
    if (material)
        if (auto* output = ns::AddRegisteredNode(fresh, registry, "material.surface.output"))
            output->SetEditorPosition(400.0f, 150.0f);
    adoptGraph(std::move(fresh), {});
    status(material ? "New material." : "New image graph.");
}

void GraphWorkspace::adoptGraph(ns::Graph newGraph, const juce::String& name)
{
    const bool material = newGraph.DiagramType() == ce::material::kMaterialDiagram;
    graph = std::move(newGraph);
    graph.SetTarget(material ? ns::GraphTarget::Material : ns::GraphTarget::Dataflow);
    graphName = name;
    edited = false;
    palette.SetDiagramType(graph.DiagramType());
    // Material Variables become shader uniforms or literals: numbers and colours.
    symbols.setAllowedTypes(material ? std::vector<ns::DataType> { ns::DataType::Float, ns::DataType::Color }
                                     : std::vector<ns::DataType> { ns::DataType::Float, ns::DataType::Int, ns::DataType::Bool,
                                                                   ns::DataType::Color, ns::DataType::String });
    if (! material)
        syncGraphNodes(); // graphs it uses may have changed since it was saved
    graphView.GraphReplaced();
    thumbnails.clear();
    errors.clear();
    preview->showNode(0, {}, {});
    properties.showNode(0);
    symbols.refresh();
    forgetDrawTargets();
    if (onTypeChanged)
        onTypeChanged();
    requestEvaluation();
}

void GraphWorkspace::writeGraph(const juce::String& name)
{
    auto* doc = new juce::DynamicObject();
    doc->setProperty("format", documentFormat);
    doc->setProperty("version", 1);
    doc->setProperty("name", name);
    doc->setProperty("graph", juce::String(ns::SerializeGraph(graph)));
    const auto json = juce::JSON::toString(juce::var(doc), false);

    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::metadata;
    options.category = "image-graph";
    options.displayName = name;
    options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(name) + ".imggraph.json";
    options.mediaType = "application/x-djehuti-image-graph+json";
    options.sourceApp = "Djehuti Texture";
    options.sourceTool = "Image Graph";
    options.description = "Image Graph node graph.";
    options.tags = { "image-graph" };

    creation::assets::AssetDescriptor saved;
    juce::String error;
    if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, juce::MemoryBlock(json.toRawUTF8(), json.getNumBytesAsUTF8()),
                                                                    options, saved, error)
        || ! projectSession->commit(error))
    {
        status("Could not save the image graph: " + error);
        return;
    }
    graphName = name;
    edited = false;
    status("Saved image graph " + name + ".");
}

// A material saves as a material asset - its graph as .frgraph text - the form other apps read.
void GraphWorkspace::writeMaterial(const juce::String& name)
{
    const auto text = juce::String(ns::SerializeGraph(graph));
    creation::assets::ProjectAssetService::ImportOptions options;
    options.kind = creation::assets::AssetKind::material;
    options.displayName = name;
    options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(name) + ".frgraph";
    options.mediaType = "application/x-creation-node-graph";
    options.sourceApp = "Djehuti Texture";
    options.sourceTool = "Material Graph";
    options.description = "Material node graph.";

    creation::assets::AssetDescriptor saved;
    juce::String error;
    if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, juce::MemoryBlock(text.toRawUTF8(), text.getNumBytesAsUTF8()),
                                                                    options, saved, error)
        || ! projectSession->commit(error))
    {
        status("Could not save the material: " + error);
        return;
    }
    graphName = name;
    edited = false;
    status("Saved material " + name + ".");
}

void GraphWorkspace::saveGraph()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    if (graphName.isEmpty())
        saveGraphAs();
    else if (isMaterial())
        writeMaterial(graphName);
    else
        writeGraph(graphName);
}

void GraphWorkspace::saveGraphAs()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    auto* prompt = new juce::AlertWindow(isMaterial() ? "Save Material" : "Save Image Graph",
                                         isMaterial() ? "Name this material:" : "Name this image graph:", juce::MessageBoxIconType::NoIcon, &graphView);
    prompt->addTextEditor("name", graphName);
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt](int result) {
        const auto name = prompt->getTextEditorContents("name").trim();
        if (result == 1 && name.isNotEmpty())
        {
            if (isMaterial())
                writeMaterial(name);
            else
                writeGraph(name);
        }
    }), true);
}

void GraphWorkspace::openGraph()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    project_images::Source graphs;
    graphs.noun = "graph";
    graphs.list = [this]() {
        juce::Array<project_images::Entry> entries;
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
        {
            if (asset.logicalPath.endsWith(".imggraph.json"))
                entries.add({ asset.displayName + "  (image graph)", asset.logicalPath });
            else if (asset.kind == creation::assets::AssetKind::material && asset.logicalPath.endsWith(".frgraph"))
                entries.add({ asset.displayName + "  (material)", asset.logicalPath });
        }
        return entries;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Open Graph";
    options.content.setOwned(new ProjectImagePicker(graphs, {}, [this](const juce::String& path) {
        juce::MemoryBlock bytes;
        if (path.isEmpty() || ! projectSession->readEntry(path, bytes))
            return;
        juce::String name;
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (asset.logicalPath == path)
                name = asset.displayName;

        if (path.endsWith(".frgraph"))
        {
            // A material asset: its graph as .frgraph text.
            std::string parseError;
            auto loaded = ns::DeserializeGraph(bytes.toString().toStdString(), parseError);
            if (loaded == nullptr)
            {
                status("Could not read the material: " + juce::String(parseError));
                return;
            }
            loaded->SetDiagramType(ce::material::kMaterialDiagram); // a material, whether or not the file says so
            adoptGraph(std::move(*loaded), name);
            status("Opened material " + name + ".");
            return;
        }
        const auto doc = juce::JSON::parse(bytes.toString());
        const auto loaded = image_graph::readGraphDocument(bytes.toString());
        if (loaded.graph == nullptr)
        {
            status("Could not read the graph: " + loaded.error);
            return;
        }
        std::string parseError;
        auto editable = ns::DeserializeGraph(loaded.text, parseError); // graphs are not copied: read the text again
        if (editable == nullptr)
            return;
        editable->SetDiagramType(image_graph::kImageDiagram);
        adoptGraph(std::move(*editable), doc["name"].toString());
        status("Opened image graph " + graphName + ".");
    }));
    options.componentToCentreAround = graphView.getTopLevelComponent();
    options.dialogBackgroundColour = juce::Colour(0xff161a1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

bool GraphWorkspace::canSaveOutput() const noexcept
{
    return hasProject() && lastPreview != nullptr;
}

// Saves the previewed output as a PNG image asset in the project.
void GraphWorkspace::saveOutputAsImage()
{
    if (! canSaveOutput())
    {
        status("Preview a node output first.");
        return;
    }
    auto* prompt = new juce::AlertWindow("Save Output as Image", "Save the previewed output as an image in the project:",
                                         juce::MessageBoxIconType::NoIcon, &graphView);
    prompt->addTextEditor("name", (graphName.isNotEmpty() ? graphName + " " : juce::String()) + juce::String(preview->getOutput()));
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt, image = lastPreview](int result) {
        const auto name = prompt->getTextEditorContents("name").trim();
        if (result != 1 || name.isEmpty() || image == nullptr)
            return;
        juce::MemoryBlock png;
        {
            juce::MemoryOutputStream stream(png, false);
            juce::PNGImageFormat format;
            if (! format.writeImageToStream(image_graph::toDisplayImage(*image), stream))
            {
                status("Could not encode the image.");
                return;
            }
        }
        creation::assets::ProjectAssetService::ImportOptions options;
        options.kind = creation::assets::AssetKind::texture;
        options.displayName = name;
        options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(name) + ".png";
        options.mediaType = "image/png";
        options.sourceApp = "Djehuti Texture";
        options.sourceTool = "Image Graph";
        options.tags = { "image-graph" };
        creation::assets::AssetDescriptor saved;
        juce::String error;
        if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, png, options, saved, error)
            || ! projectSession->commit(error))
        {
            status("Could not save the image: " + error);
            return;
        }
        status("Saved " + name + " to the project.");
    }), true);
}

//==============================================================================
// Render Outputs: evaluates every Output node on its own thread (with a progress window), encodes them, then
// saves each on the message thread.
class GraphWorkspace::RenderJob final : public juce::ThreadWithProgressWindow
{
public:
    struct Rendered
    {
        juce::String name;
        juce::MemoryBlock png;
    };

    RenderJob(const image_graph::Library& lib, std::string text, std::vector<std::pair<ns::NodeId, juce::String>> targets,
              creation::assets::ProjectSession& session, std::function<void(std::vector<Rendered>, juce::String)> done)
        : juce::ThreadWithProgressWindow("Rendering the graph's outputs...", true, true),
          library(lib), graphText(std::move(text)), outputs(std::move(targets)), project(session), onDone(std::move(done)) {}

    void run() override
    {
        setStatusMessage("Preparing the node routines");
        image_graph::Host host;
        host.loadImage = [this](const juce::String& path) -> image_graph::ImagePtr {
            juce::MemoryBlock bytes;
            if (! project.readEntry(path, bytes))
                return nullptr;
            return image_graph::fromDisplayImage(juce::ImageFileFormat::loadFrom(bytes.getData(), bytes.getSize()));
        };
        host.loadGraph = [this](const juce::String& path) {
            juce::MemoryBlock bytes;
            if (! project.readEntry(path, bytes))
                return image_graph::Host::LoadedGraph {};
            return image_graph::readGraphDocument(bytes.toString());
        };
        image_graph::Evaluator evaluator(library, host);
        std::string parseError;
        auto graph = ns::DeserializeGraph(graphText, parseError);
        if (graph == nullptr)
        {
            error = "Could not read the graph: " + juce::String(parseError);
            return;
        }
        for (size_t i = 0; i < outputs.size() && ! threadShouldExit(); ++i)
        {
            const auto id = outputs[i].first;
            const auto name = outputs[i].second;
            const double done = static_cast<double>(i) / static_cast<double>(outputs.size());
            setProgress(done);
            setStatusMessage(juce::String(juce::roundToInt(100.0 * done)) + "%  -  computing " + name);
            juce::String nodeError;
            auto image = evaluator.evaluate(*graph, id, "image", nodeError);
            if (image == nullptr)
            {
                error = name + ": " + nodeError;
                return;
            }
            Rendered r;
            r.name = name;
            bool encoded = false;
            if (image->data)
            {
                std::vector<float> grey(static_cast<size_t>(image->width) * static_cast<size_t>(image->height));
                for (size_t k = 0; k < grey.size(); ++k)
                    grey[k] = image->rgba[k * 4];
                encoded = texture_set::encodeGray16Png(grey, image->width, image->height, r.png);
            }
            else
            {
                juce::MemoryOutputStream stream(r.png, false);
                juce::PNGImageFormat format;
                encoded = format.writeImageToStream(image_graph::toDisplayImage(*image), stream);
            }
            if (! encoded)
            {
                error = "Could not encode " + name + ".";
                return;
            }
            rendered.push_back(std::move(r));
        }
        setProgress(1.0);
    }

    void threadComplete(bool cancelled) override
    {
        if (onDone)
            onDone(cancelled ? std::vector<Rendered> {} : std::move(rendered), cancelled ? juce::String("Cancelled.") : error);
        delete this;
    }

private:
    const image_graph::Library& library;
    std::string graphText;
    std::vector<std::pair<ns::NodeId, juce::String>> outputs;
    creation::assets::ProjectSession& project;
    std::function<void(std::vector<Rendered>, juce::String)> onDone;
    std::vector<Rendered> rendered;
    juce::String error;
};

void GraphWorkspace::renderOutputs()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }

    std::vector<std::pair<ns::NodeId, juce::String>> outputs;
    for (const auto& [id, node] : graph.Nodes())
    {
        if (node->TypeName() != "image.output")
            continue;
        juce::String name;
        for (const auto& pin : node->Inputs())
            if (pin.name == "name" && std::holds_alternative<std::string>(pin.defaultValue))
                name = juce::String(std::get<std::string>(pin.defaultValue)).trim();
        if (name.isEmpty())
            name = (graphName.isNotEmpty() ? graphName : juce::String("image graph")) + " output " + juce::String(static_cast<int>(id));
        outputs.push_back({ id, name });
    }
    if (outputs.empty())
    {
        status("Add an Output node (Nodes > Output), wire an image into it and give it a name.");
        return;
    }

    auto* job = new RenderJob(library, ns::SerializeGraph(graph), outputs, *projectSession,
                              [this](std::vector<RenderJob::Rendered> rendered, juce::String error) {
        if (error.isNotEmpty())
        {
            status("Render Outputs: " + error);
            return;
        }
        for (const auto& r : rendered)
        {
            creation::assets::ProjectAssetService::ImportOptions options;
            options.kind = creation::assets::AssetKind::texture;
            options.displayName = r.name;
            options.logicalPath = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot) + slugFor(r.name) + ".png";
            options.mediaType = "image/png";
            options.sourceApp = "Djehuti Texture";
            options.sourceTool = "Image Graph";
            options.description = "Rendered from image graph " + (graphName.isNotEmpty() ? graphName : juce::String("(unsaved)")) + ".";
            options.tags = { "image-graph" };
            creation::assets::AssetDescriptor saved;
            juce::String saveError;
            if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, r.png, options, saved, saveError))
            {
                status("Could not save " + r.name + ": " + saveError);
                return;
            }
        }
        juce::String commitError;
        if (! projectSession->commit(commitError))
        {
            status("Could not save the outputs: " + commitError);
            return;
        }
        status("Rendered " + juce::String(static_cast<int>(rendered.size())) + " output(s) into the project.");
    });
    job->launchThread();
}
