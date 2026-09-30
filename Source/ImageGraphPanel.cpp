#include "ImageGraphPanel.h"

#include <TextureSet.h>
#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>
#include <node_system/frgraph_serialization.h>

namespace ns = ce::node_system;

namespace
{
constexpr const char* documentFormat = "djehuti-image-graph";
constexpr float thumbnailHeight = 100.0f;
const juce::Colour panelBackground { 0xff1e2227 };

// A small display thumbnail straight from the float image (nearest sample), without converting the whole image.
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
class ImageGraphWorkspace::Worker final : public juce::Thread, public juce::ChangeBroadcaster
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
    };

    Worker(const image_graph::Library& lib, std::function<creation::assets::ProjectSession*()> session)
        : juce::Thread("Image Graph"), library(lib), projectSession(std::move(session))
    {
        startThread();
    }

    ~Worker() override { stopThread(4000); }

    void request(std::string graphText, ns::NodeId previewNode, std::string previewOutput)
    {
        {
            const juce::ScopedLock lock(requestLock);
            pending = { std::move(graphText), previewNode, std::move(previewOutput), true };
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
class ImageGraphWorkspace::PreviewPanel final : public juce::Component
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
    ns::NodeTypeRegistry registry;
    library.registerTypes(registry);
    return registry;
}
}

ImageGraphWorkspace::ImageGraphWorkspace()
    : registry(makeRegistry(library)),
      palette(registry),
      preview(std::make_unique<PreviewPanel>()),
      worker(std::make_unique<Worker>(library, [this]() { return projectSession; }))
{
    graphView.onGraphChanged = [this]() {
        properties.refresh(); // wiring changed: which inputs are editable changed
        graphEdited();
    };
    graphView.onSelectionChanged = [this](ns::NodeId id) { selectionChanged(id); };
    graphView.onGetNodeExtraHeight = [this](ns::NodeId id) {
        const auto* node = graph.FindNode(id);
        return node != nullptr && ! node->Outputs().empty() ? thumbnailHeight : 0.0f;
    };
    graphView.onPaintNode = [this](juce::Graphics& g, ns::NodeId id, juce::Rectangle<float> bounds) {
        auto area = bounds.withTrimmedTop(bounds.getHeight() - thumbnailHeight).reduced(8.0f, 6.0f);
        auto error = errors.find(id);
        if (error != errors.end())
        {
            g.setColour(juce::Colour(0xffff8a80));
            g.setFont(juce::FontOptions(11.0f));
            g.drawFittedText(error->second, area.toNearestInt(), juce::Justification::centred, 4);
            return;
        }
        auto thumb = thumbnails.find(id);
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
    worker->addChangeListener(this);
}

ImageGraphWorkspace::~ImageGraphWorkspace()
{
    worker->removeChangeListener(this);
    worker.reset();
}

juce::Component& ImageGraphWorkspace::getPreview() noexcept { return *preview; }

void ImageGraphWorkspace::setProjectSession(creation::assets::ProjectSession* session)
{
    projectSession = session;
}

void ImageGraphWorkspace::setImageSource(project_images::Source source)
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

void ImageGraphWorkspace::status(const juce::String& text)
{
    if (onStatus)
        onStatus(text);
}

juce::String ImageGraphWorkspace::getTitle() const
{
    return (graphName.isNotEmpty() ? graphName : juce::String("Untitled image graph")) + (edited ? " *" : "");
}

void ImageGraphWorkspace::graphEdited()
{
    edited = true;
    graphView.repaint();
    requestEvaluation();
}

void ImageGraphWorkspace::selectionChanged(ns::NodeId id)
{
    selectedNode = id;
    properties.showNode(id);
    if (preview->isPinned() || id == 0)
        return;
    const auto* node = graph.FindNode(id);
    if (node == nullptr)
        return;
    std::vector<std::string> outputs;
    for (const auto& pin : node->Outputs())
        outputs.push_back(pin.name);
    const auto* descriptor = registry.Find(node->TypeName());
    preview->showNode(id, descriptor != nullptr ? juce::String(descriptor->displayName) : juce::String(node->TypeName()), outputs);
    requestEvaluation();
}

void ImageGraphWorkspace::requestEvaluation()
{
    worker->request(ns::SerializeGraph(graph), preview->getNode(), preview->getOutput());
}

void ImageGraphWorkspace::changeListenerCallback(juce::ChangeBroadcaster*)
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
            preview->setImage(result.previewDisplay, juce::String(result.previewOutput) + "  " + juce::String(result.previewImage->width)
                                                         + " x " + juce::String(result.previewImage->height)
                                                         + (result.previewImage->data ? "  (data)" : ""));
        else
            preview->setImage({}, result.previewError);
        lastPreview = result.previewImage;
    }
}

// Node-specific editors in Properties.
std::unique_ptr<juce::Component> ImageGraphWorkspace::customEditor(ns::Node& node, const ns::Pin& pin, int& height)
{
    const auto type = node.TypeName();

    // Surface Map: load settings from a saved surface map.
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
    if (pin.type.dataType == ns::DataType::Texture && type != "image.load")
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

void ImageGraphWorkspace::chooseSurfaceMapFor(ns::NodeId id)
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
void ImageGraphWorkspace::newGraph()
{
    graph = ns::Graph("Image Graph", ns::GraphTarget::Dataflow);
    graphView.GraphReplaced();
    graphName.clear();
    edited = false;
    thumbnails.clear();
    errors.clear();
    preview->showNode(0, {}, {});
    properties.showNode(0);
    status("New image graph.");
}

void ImageGraphWorkspace::writeGraph(const juce::String& name)
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

void ImageGraphWorkspace::saveGraph()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    if (graphName.isEmpty())
        saveGraphAs();
    else
        writeGraph(graphName);
}

void ImageGraphWorkspace::saveGraphAs()
{
    if (! hasProject())
    {
        status("Open a project first.");
        return;
    }
    auto* prompt = new juce::AlertWindow("Save Image Graph", "Name this image graph:", juce::MessageBoxIconType::NoIcon, &graphView);
    prompt->addTextEditor("name", graphName);
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt](int result) {
        const auto name = prompt->getTextEditorContents("name").trim();
        if (result == 1 && name.isNotEmpty())
            writeGraph(name);
    }), true);
}

void ImageGraphWorkspace::openGraph()
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
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (asset.logicalPath.endsWith(".imggraph.json"))
                entries.add({ asset.displayName, asset.logicalPath });
        return entries;
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Open Image Graph";
    options.content.setOwned(new ProjectImagePicker(graphs, {}, [this](const juce::String& path) {
        juce::MemoryBlock bytes;
        if (path.isEmpty() || ! projectSession->readEntry(path, bytes))
            return;
        const auto doc = juce::JSON::parse(bytes.toString());
        if (doc["format"].toString() != documentFormat)
        {
            status(path + " is not an image graph.");
            return;
        }
        std::string parseError;
        auto loaded = ns::DeserializeGraph(doc["graph"].toString().toStdString(), parseError);
        if (loaded == nullptr)
        {
            status("Could not read the graph: " + juce::String(parseError));
            return;
        }
        graph = std::move(*loaded);
        graph.SetTarget(ns::GraphTarget::Dataflow);
        graphView.GraphReplaced();
        graphName = doc["name"].toString();
        edited = false;
        thumbnails.clear();
        errors.clear();
        preview->showNode(0, {}, {});
        properties.showNode(0);
        requestEvaluation();
        status("Opened image graph " + graphName + ".");
    }));
    options.componentToCentreAround = graphView.getTopLevelComponent();
    options.dialogBackgroundColour = juce::Colour(0xff161a1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

bool ImageGraphWorkspace::canSaveOutput() const noexcept
{
    return hasProject() && lastPreview != nullptr;
}

// Saves the previewed output as a PNG image asset in the project.
void ImageGraphWorkspace::saveOutputAsImage()
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
class ImageGraphWorkspace::RenderJob final : public juce::ThreadWithProgressWindow
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

void ImageGraphWorkspace::renderOutputs()
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
