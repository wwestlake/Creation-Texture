#include "SurfaceMapPanel.h"

#include <ImageLabCore.h>
#include <TextureSet.h>
#include <creation/assets/ProjectAssetService.h>
#include <creation/assets/ProjectManifest.h>

#include <map>

namespace
{
const juce::Colour panelBackground { 0xff1e2227 };
constexpr int previewLimit = 1024;

juce::Image limitSize(const juce::Image& image, int limit)
{
    const int longest = juce::jmax(image.getWidth(), image.getHeight());
    if (longest <= limit)
        return image;
    const float scale = static_cast<float>(limit) / static_cast<float>(longest);
    return image.rescaled(juce::jmax(3, juce::roundToInt(static_cast<float>(image.getWidth()) * scale)),
                          juce::jmax(3, juce::roundToInt(static_cast<float>(image.getHeight()) * scale)),
                          juce::Graphics::highResamplingQuality);
}
}

//==============================================================================
// Recomputes the preview maps in the background. Only the latest request matters: slider moves while it computes
// just replace the pending request.
class SurfaceMapWorkspace::Worker final : public juce::Thread, public juce::ChangeBroadcaster
{
public:
    Worker() : juce::Thread("Surface Map") { startThread(); }
    ~Worker() override { stopThread(4000); }

    void request(std::vector<float> pixels, int width, int height, surface_maps::Settings settings)
    {
        {
            const juce::ScopedLock lock(requestLock);
            pending = { std::move(pixels), width, height, settings, true };
        }
        notify();
    }

    bool takeResult(surface_maps::Maps& out, juce::String& error)
    {
        const juce::ScopedLock lock(resultLock);
        if (! hasResult)
            return false;
        out = std::move(result);
        error = resultError;
        hasResult = false;
        return true;
    }

    bool isBusy() const noexcept { return busy; }

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
            if (! engine)
                engine = std::make_unique<surface_maps::Engine>();
            surface_maps::Maps maps;
            juce::String error;
            engine->compute(job.pixels, job.width, job.height, job.settings, maps, error);
            {
                const juce::ScopedLock lock(resultLock);
                result = std::move(maps);
                resultError = error;
                hasResult = true;
            }
            busy = false;
            sendChangeMessage();
        }
    }

private:
    struct Request
    {
        std::vector<float> pixels;
        int width = 0, height = 0;
        surface_maps::Settings settings;
        bool valid = false;
    };

    std::unique_ptr<surface_maps::Engine> engine; // compiled on the worker thread, on first use
    juce::CriticalSection requestLock, resultLock;
    Request pending;
    surface_maps::Maps result;
    juce::String resultError;
    bool hasResult = false;
    std::atomic<bool> busy { false };
};


// A setting as two knobs and a type-in box, VST style: Coarse covers the whole range, Fine adds up to +/-5% of it,
// and the box shows (and takes) the resulting value. Typing sets Coarse to the value and centres Fine;
// double-clicking a knob resets it (Coarse to the default, Fine to centre).
class KnobPair final : public juce::Component
{
public:
    static constexpr int rowHeight = 62;

    KnobPair(double lo, double hi, double value, double defaultValue, std::function<void(float)> onChange)
        : low(lo), high(hi), changed(std::move(onChange))
    {
        const double span = (hi - lo) * 0.05;
        setUp(coarse, lo, hi, value, defaultValue);
        setUp(fine, -span, span, 0.0, 0.0);
        fine.setMouseDragSensitivity(400);

        for (auto* caption : { &coarseCaption, &fineCaption })
        {
            caption->setJustificationType(juce::Justification::centred);
            caption->setFont(juce::FontOptions(11.0f));
            caption->setColour(juce::Label::textColourId, juce::Colours::grey);
            addAndMakeVisible(*caption);
        }

        box.setJustification(juce::Justification::centred);
        box.setInputRestrictions(10, "0123456789.-");
        box.onReturnKey = [this]() { typed(); };
        box.onFocusLost = [this]() { typed(); };
        addAndMakeVisible(box);
        showValue();
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto knobArea = [&area](juce::Rectangle<int>& knob, juce::Label& caption) {
            auto column = area.removeFromLeft(52);
            caption.setBounds(column.removeFromBottom(14));
            knob = column.withSizeKeepingCentre(46, 46);
        };
        juce::Rectangle<int> coarseArea, fineArea;
        knobArea(coarseArea, coarseCaption);
        knobArea(fineArea, fineCaption);
        coarse.setBounds(coarseArea);
        fine.setBounds(fineArea);
        area.removeFromLeft(6);
        box.setBounds(area.withSizeKeepingCentre(juce::jmin(area.getWidth(), 70), 26));
    }

private:
    void setUp(juce::Slider& knob, double lo, double hi, double value, double resetTo)
    {
        knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        knob.setRange(lo, hi, 0.0);
        knob.setValue(value, juce::dontSendNotification);
        knob.setDoubleClickReturnValue(true, resetTo);
        knob.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff5b8fc7));
        knob.onValueChange = [this]() { valueChanged(); };
        addAndMakeVisible(knob);
    }

    float combined() const { return static_cast<float>(juce::jlimit(low, high, coarse.getValue() + fine.getValue())); }

    void showValue() { box.setText(juce::String(combined(), 3), juce::dontSendNotification); }

    void valueChanged()
    {
        showValue();
        if (changed)
            changed(combined());
    }

    void typed()
    {
        const auto text = box.getText().trim();
        if (text.isEmpty())
        {
            showValue();
            return;
        }
        fine.setValue(0.0, juce::dontSendNotification);
        coarse.setValue(juce::jlimit(low, high, text.getDoubleValue()), juce::dontSendNotification);
        valueChanged();
    }

    double low, high;
    std::function<void(float)> changed;
    juce::Slider coarse, fine;
    juce::Label coarseCaption { {}, "Coarse" }, fineCaption { {}, "Fine" };
    juce::TextEditor box;
};

//==============================================================================
// Source image slot, raised/sunken, and CrazyBump's controls as knob pairs.
class SurfaceMapWorkspace::SettingsPanel final : public juce::Component
{
public:
    explicit SettingsPanel(SurfaceMapWorkspace& w) : workspace(w)
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        addAndMakeVisible(viewport);
        rebuild();
    }

    void rebuild()
    {
        rows.clear();
        content.removeAllChildren();
        const auto& s = workspace.getSettings();
        const surface_maps::Settings defaults;

        heading("Source image");
        auto slot = std::make_unique<ProjectImageSlot>(workspace.getImageSource(), workspace.getSourcePath(),
                                                       [this](const juce::String& path) {
                                                           workspace.setSource(path);
                                                           juce::MessageManager::callAsync([safe = juce::Component::SafePointer<SettingsPanel>(this)]() {
                                                               if (safe != nullptr) safe->rebuild();
                                                           });
                                                       });
        add(std::move(slot), ProjectImageSlot::preferredHeight);

        heading("Height");
        auto shape = std::make_unique<ShapeChoice>(s.sunken, [this](bool sunken) { edit([sunken](auto& st) { st.sunken = sunken; }); });
        add(std::move(shape), 28);
        slider("Intensity", 0.0, 4.0, s.intensity, defaults.intensity, [](auto& st, float v) { st.intensity = v; });
        slider("Sharpen", 0.0, 2.0, s.sharpen, defaults.sharpen, [](auto& st, float v) { st.sharpen = v; });
        slider("Noise Removal", 0.0, 8.0, s.noiseRemoval, defaults.noiseRemoval, [](auto& st, float v) { st.noiseRemoval = v; });
        slider("Fine Detail", 0.0, 1.0, s.fine, defaults.fine, [](auto& st, float v) { st.fine = v; });
        slider("Medium Detail", 0.0, 1.0, s.medium, defaults.medium, [](auto& st, float v) { st.medium = v; });
        slider("Large Detail", 0.0, 1.0, s.large, defaults.large, [](auto& st, float v) { st.large = v; });
        slider("Very Large Detail", 0.0, 1.0, s.veryLarge, defaults.veryLarge, [](auto& st, float v) { st.veryLarge = v; });
        slider("Huge Detail", 0.0, 1.0, s.huge, defaults.huge, [](auto& st, float v) { st.huge = v; });

        heading("Normal");
        auto dx = std::make_unique<juce::ToggleButton>("DirectX green (flip Y)");
        dx->setToggleState(s.directX, juce::dontSendNotification);
        dx->onClick = [this, b = dx.get()]() { const bool on = b->getToggleState(); edit([on](auto& st) { st.directX = on; }); };
        add(std::move(dx), 26);

        heading("Occlusion");
        slider("Strength", 0.0, 2.0, s.occlusion, defaults.occlusion, [](auto& st, float v) { st.occlusion = v; });

        heading("Specular");
        slider("Level", 0.0, 1.0, s.specularLevel, defaults.specularLevel, [](auto& st, float v) { st.specularLevel = v; });
        slider("Contrast", 0.0, 12.0, s.specularContrast, defaults.specularContrast, [](auto& st, float v) { st.specularContrast = v; });

        heading("Diffuse");
        slider("De-light", 0.0, 1.0, s.delight, defaults.delight, [](auto& st, float v) { st.delight = v; });

        heading("Metallic");
        slider("Metallic", 0.0, 1.0, s.metallic, defaults.metallic, [](auto& st, float v) { st.metallic = v; });

        layout();
    }

    void paint(juce::Graphics& g) override { g.fillAll(panelBackground); }
    void resized() override
    {
        viewport.setBounds(getLocalBounds());
        layout();
    }

private:
    // Two buttons for CrazyBump's "which one looks right".
    class ShapeChoice final : public juce::Component
    {
    public:
        ShapeChoice(bool sunken, std::function<void(bool)> onChange) : changed(std::move(onChange))
        {
            for (auto* b : { &raised, &sunk })
            {
                b->setClickingTogglesState(true);
                b->setRadioGroupId(1);
                b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2f5d8a));
                addAndMakeVisible(*b);
            }
            raised.setConnectedEdges(juce::Button::ConnectedOnRight);
            sunk.setConnectedEdges(juce::Button::ConnectedOnLeft);
            (sunken ? sunk : raised).setToggleState(true, juce::dontSendNotification);
            raised.onClick = [this]() { if (raised.getToggleState() && changed) changed(false); };
            sunk.onClick = [this]() { if (sunk.getToggleState() && changed) changed(true); };
        }
        void resized() override
        {
            auto area = getLocalBounds();
            raised.setBounds(area.removeFromLeft(area.getWidth() / 2));
            sunk.setBounds(area);
        }

    private:
        juce::TextButton raised { "Raised" }, sunk { "Sunken" };
        std::function<void(bool)> changed;
    };

    struct Row
    {
        std::unique_ptr<juce::Component> label;
        std::unique_ptr<juce::Component> editor;
        int height = 26;
    };

    template <typename Fn>
    void edit(Fn change)
    {
        auto s = workspace.getSettings();
        change(s);
        workspace.setSettings(s);
    }

    void heading(const juce::String& text)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText(text, juce::dontSendNotification);
        label->setFont(juce::FontOptions(14.0f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, juce::Colour(0xff9ecbff));
        content.addAndMakeVisible(*label);
        rows.push_back({ std::move(label), nullptr, 24 });
    }

    void add(std::unique_ptr<juce::Component> editor, int height)
    {
        content.addAndMakeVisible(*editor);
        rows.push_back({ nullptr, std::move(editor), height });
    }

    void slider(const juce::String& name, double lo, double hi, float value, float defaultValue,
                std::function<void(surface_maps::Settings&, float)> setter)
    {
        auto label = std::make_unique<juce::Label>();
        label->setText(name, juce::dontSendNotification);
        label->setColour(juce::Label::textColourId, juce::Colours::white);
        label->setJustificationType(juce::Justification::centredLeft);
        auto knobs = std::make_unique<KnobPair>(lo, hi, value, defaultValue, [this, setter](float v) {
            edit([&setter, v](auto& st) { setter(st, v); });
        });
        content.addAndMakeVisible(*label);
        content.addAndMakeVisible(*knobs);
        rows.push_back({ std::move(label), std::move(knobs), KnobPair::rowHeight });
    }

    void layout()
    {
        const int width = juce::jmax(220, viewport.getWidth() - viewport.getScrollBarThickness());
        int y = 8;
        for (auto& row : rows)
        {
            if (row.label != nullptr && row.editor != nullptr)
            {
                // The setting's name on its own line, its knobs and value box below it.
                row.label->setBounds(8, y, width - 16, 18);
                y += 18;
                row.editor->setBounds(8, y, width - 16, row.height);
            }
            else if (row.label != nullptr)
            {
                row.label->setBounds(8, y + 4, width - 16, row.height);
            }
            else if (row.editor != nullptr)
            {
                row.editor->setBounds(8, y, width - 16, row.height);
            }
            y += row.height + 6;
        }
        content.setSize(width, y + 8);
    }

    SurfaceMapWorkspace& workspace;
    juce::Viewport viewport;
    juce::Component content;
    std::vector<Row> rows;
};

//==============================================================================
// Which map to look at, and a large view of it.
class SurfaceMapWorkspace::MapsPanel final : public juce::Component
{
public:
    MapsPanel()
    {
        const char* names[] = { "Normal", "Height", "Occlusion", "Roughness", "Specular", "Diffuse", "ORM" };
        for (int i = 0; i < 7; ++i)
        {
            auto* b = buttons.add(new juce::TextButton(names[i]));
            b->setClickingTogglesState(true);
            b->setRadioGroupId(2);
            b->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2f5d8a));
            b->onClick = [this, i]() { if (buttons[i]->getToggleState()) { shown = i; updateImage(); } };
            addAndMakeVisible(b);
        }
        buttons[0]->setToggleState(true, juce::dontSendNotification);
        statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(statusLabel);
    }

    void setMaps(const surface_maps::Maps* newMaps)
    {
        maps = newMaps;
        updateImage();
    }

    void setStatus(const juce::String& text) { statusLabel.setText(text, juce::dontSendNotification); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff111317));
        if (image.isValid())
        {
            g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
            g.drawImage(image, imageArea.toFloat(), juce::RectanglePlacement::centred);
        }
        else
        {
            g.setColour(juce::Colours::grey);
            g.drawFittedText("Choose a source image in Settings.", imageArea, juce::Justification::centred, 2);
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop(32).reduced(4);
        const int w = bar.getWidth() / buttons.size();
        for (auto* b : buttons)
            b->setBounds(bar.removeFromLeft(w).reduced(1, 0));
        statusLabel.setBounds(area.removeFromBottom(22).reduced(8, 0));
        imageArea = area.reduced(8);
    }

private:
    void updateImage()
    {
        image = {};
        if (maps != nullptr && maps->isValid())
        {
            const int w = maps->width, h = maps->height;
            switch (shown)
            {
                case 0: image = texture_set::rgbaToRawImage(maps->normal, w, h); break;
                case 1: image = texture_set::grayToImage(maps->heightMap, w, h); break;
                case 2: image = texture_set::grayToImage(maps->occlusion, w, h); break;
                case 3: image = texture_set::grayToImage(maps->roughness, w, h); break;
                case 4: image = texture_set::grayToImage(maps->specular, w, h); break;
                case 5: image = texture_set::rgbaToSrgbImage(maps->diffuse, w, h); break;
                default: image = texture_set::packOrm(*maps); break;
            }
        }
        repaint();
    }

    juce::OwnedArray<juce::TextButton> buttons;
    juce::Label statusLabel;
    const surface_maps::Maps* maps = nullptr;
    juce::Image image;
    juce::Rectangle<int> imageArea;
    int shown = 0;
};

//==============================================================================
// Full-size compute and pack build for Save, with a progress window.
class SurfaceMapWorkspace::SaveJob final : public juce::ThreadWithProgressWindow
{
public:
    SaveJob(juce::Image source, surface_maps::Settings s, juce::String n, juce::String src, creation::assets::ProjectSession& session,
            std::function<void(bool, const texture_set::Pack&, const juce::String&)> done)
        : juce::ThreadWithProgressWindow("Making the surface map at full size...", true, true),
          image(std::move(source)), settings(s), name(std::move(n)), sourceAsset(std::move(src)), project(session),
          onDone(std::move(done)) {}

    // Progress: reading the image 0-5%, the maps 5-40%, encoding the six map files 40-75%, writing them into the
    // project 75-100%. The manifest (the asset itself) is saved afterwards on the message thread.
    void run() override
    {
        step(0.0f, "Reading the image");
        const auto layer = image_lab::layerFromImage(image, name);
        step(0.03f, "Preparing the surface map routines");
        surface_maps::Engine engine;
        surface_maps::Maps maps;
        ok = engine.compute(layer.pixels, layer.width, layer.height, settings, maps, error,
                            [this](float f, const juce::String& s) { step(0.05f + 0.35f * f, s); });
        // Saving over an existing set makes its next version; the earlier versions' files are kept.
        juce::var previous;
        juce::MemoryBlock existing;
        const auto base = juce::String(creation::assets::ProjectContainerPaths::sourceAssetRoot);
        if (project.readEntry(texture_set::manifestPathFor(base, name), existing))
            previous = juce::JSON::parse(existing.toString());

        if (ok && ! threadShouldExit())
            ok = texture_set::build(maps, settings, name, base, sourceAsset, pack, error,
                                    [this](float f, const juce::String& s) { step(0.4f + 0.35f * f, s); }, previous);

        // PNGs are already compressed, so the store is told not to compress them again.
        for (size_t i = 0; ok && i < pack.maps.size() && ! threadShouldExit(); ++i)
        {
            const auto& file = pack.maps[i];
            step(0.75f + 0.25f * static_cast<float>(i) / static_cast<float>(pack.maps.size()),
                 "Writing " + file.logicalPath.fromLastOccurrenceOf("/", false, false) + " into the project");
            if (! project.writeEntry(file.logicalPath, file.bytes, juce::Time::getCurrentTime(), 0))
            {
                ok = false;
                error = "Could not write " + file.logicalPath + ": " + project.getLastWriteError();
            }
        }
        if (ok)
            step(1.0f, "Saving the texture set");
    }

    void step(float fraction, const juce::String& text)
    {
        setProgress(fraction);
        setStatusMessage(juce::String(juce::roundToInt(fraction * 100.0f)) + "%  -  " + text);
    }

    void threadComplete(bool userPressedCancel) override
    {
        if (onDone)
            onDone(ok && ! userPressedCancel, pack, userPressedCancel ? juce::String("Cancelled.") : error);
        delete this;
    }

private:
    juce::Image image;
    surface_maps::Settings settings;
    juce::String name, sourceAsset, error;
    creation::assets::ProjectSession& project;
    texture_set::Pack pack;
    bool ok = false;
    std::function<void(bool, const texture_set::Pack&, const juce::String&)> onDone;
};

//==============================================================================
SurfaceMapWorkspace::SurfaceMapWorkspace()
    : worker(std::make_unique<Worker>()),
      settingsPanel(std::make_unique<SettingsPanel>(*this)),
      mapsPanel(std::make_unique<MapsPanel>())
{
    worker->addChangeListener(this);
}

SurfaceMapWorkspace::~SurfaceMapWorkspace()
{
    worker->removeChangeListener(this);
    worker.reset();
}

juce::Component& SurfaceMapWorkspace::getSettingsPanel() noexcept { return *settingsPanel; }
juce::Component& SurfaceMapWorkspace::getMapsPanel() noexcept { return *mapsPanel; }

void SurfaceMapWorkspace::setImageSource(project_images::Source source)
{
    images = std::move(source);
    settingsPanel->rebuild();
}

void SurfaceMapWorkspace::status(const juce::String& text)
{
    if (onStatus)
        onStatus(text);
}

void SurfaceMapWorkspace::setSettings(const surface_maps::Settings& newSettings)
{
    settings = newSettings;
    requestMaps();
}

void SurfaceMapWorkspace::setSource(const juce::String& logicalPath)
{
    if (logicalPath != sourcePath)
        openSetName.clear();
    sourcePath = logicalPath;
    sourceName.clear();
    sourceImage = {};
    previewPixels.clear();
    previewWidth = previewHeight = 0;
    maps = {};
    mapsPanel->setMaps(nullptr);
    preview.setMaps({}, {}, {}, false);

    if (logicalPath.isEmpty() || ! images.image)
        return;

    sourceImage = images.image(logicalPath);
    if (! sourceImage.isValid())
    {
        status("Could not read " + logicalPath + ".");
        return;
    }
    if (images.list)
        for (const auto& entry : images.list())
            if (entry.logicalPath == logicalPath)
                sourceName = entry.displayName;
    if (sourceName.isEmpty())
        sourceName = logicalPath.fromLastOccurrenceOf("/", false, false).upToLastOccurrenceOf(".", false, false);

    const auto layer = image_lab::layerFromImage(limitSize(sourceImage, previewLimit), sourceName);
    previewPixels = layer.pixels;
    previewWidth = layer.width;
    previewHeight = layer.height;
    requestMaps();
}

void SurfaceMapWorkspace::requestMaps()
{
    if (previewPixels.empty())
        return;
    mapsPanel->setStatus("Computing maps...");
    worker->request(previewPixels, previewWidth, previewHeight, settings);
}

void SurfaceMapWorkspace::changeListenerCallback(juce::ChangeBroadcaster*)
{
    surface_maps::Maps result;
    juce::String error;
    if (worker->takeResult(result, error))
    {
        mapsError = error;
        if (error.isEmpty() && result.isValid())
        {
            maps = std::move(result);
            mapsPanel->setMaps(&maps);
            preview.setMaps(texture_set::rgbaToSrgbImage(maps.diffuse, maps.width, maps.height),
                            texture_set::rgbaToRawImage(maps.normal, maps.width, maps.height),
                            texture_set::packOrm(maps), settings.directX);
        }
    }

    if (mapsError.isNotEmpty())
        mapsPanel->setStatus(mapsError);
    else if (worker->isBusy())
        mapsPanel->setStatus("Computing maps...");
    else if (maps.isValid())
        mapsPanel->setStatus(sourceName + "  -  preview " + juce::String(maps.width) + " x " + juce::String(maps.height)
                             + "  (saved at full size " + juce::String(sourceImage.getWidth()) + " x " + juce::String(sourceImage.getHeight()) + ")");
}

bool SurfaceMapWorkspace::canSave() const noexcept
{
    return projectSession != nullptr && projectSession->isValid() && sourceImage.isValid();
}

void SurfaceMapWorkspace::saveSurfaceMap()
{
    if (! canSave())
    {
        status("Choose a source image in an open project first.");
        return;
    }

    auto* prompt = new juce::AlertWindow("Save Surface Map", "Save these maps as one texture set in the project:",
                                         juce::MessageBoxIconType::NoIcon, mapsPanel.get());
    prompt->addTextEditor("name", openSetName.isNotEmpty() ? openSetName : sourceName);
    prompt->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    prompt->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    prompt->enterModalState(true, juce::ModalCallbackFunction::create([this, prompt](int result) {
        const auto name = prompt->getTextEditorContents("name").trim();
        if (result != 1 || name.isEmpty())
            return;

        auto* job = new SaveJob(sourceImage, settings, name, sourcePath, *projectSession,
                                [this, name](bool ok, const texture_set::Pack& pack, const juce::String& error) {
            if (! ok)
            {
                status("Could not make the surface map: " + error);
                return;
            }

            // The maps were written by the job; now the manifest - the asset itself - so it never points at missing files.
            creation::assets::ProjectAssetService::ImportOptions options;
            options.kind = creation::assets::AssetKind::texture;
            options.displayName = name;
            options.logicalPath = pack.manifest.logicalPath;
            options.mediaType = "application/x-djehuti-texture-set+json";
            options.category = "texture-set";
            options.sourceApp = "Djehuti Texture";
            options.sourceTool = "Surface Map";
            options.description = "Texture set: base colour, normal, height (16-bit), occlusion, roughness, packed ORM.";
            options.tags = { "texture-set", "surface-map" };

            creation::assets::AssetDescriptor saved;
            juce::String saveError;
            if (! creation::assets::ProjectAssetService::saveGeneratedAsset(*projectSession, pack.manifest.bytes, options, saved, saveError)
                || ! projectSession->commit(saveError))
            {
                status("Could not save the texture set: " + saveError);
                return;
            }
            openSetName = name;
            status("Saved texture set " + name + ", version " + juce::String(pack.revision) + ".");
        });
        job->launchThread();
    }), true);
}

void SurfaceMapWorkspace::openSurfaceMap()
{
    if (! canOpen())
    {
        status("Open a project first.");
        return;
    }

    // The project's texture sets, shown by their base colour map.
    project_images::Source sets;
    sets.noun = "texture set";
    sets.list = [this]() {
        juce::Array<project_images::Entry> entries;
        for (const auto& asset : projectSession->getManifest().assetCatalog.assets)
            if (asset.logicalPath.endsWith(".texset.json"))
                entries.add({ asset.displayName, asset.logicalPath });
        return entries;
    };
    // Each set's current base colour map, as its manifest names it (maps live in a per-version folder).
    auto paths = std::make_shared<std::map<juce::String, juce::String>>();
    auto baseColourOf = [this, paths](const juce::String& manifestPath) {
        auto found = paths->find(manifestPath);
        if (found != paths->end())
            return found->second;
        juce::MemoryBlock bytes;
        juce::String path;
        if (projectSession->readEntry(manifestPath, bytes))
            path = juce::JSON::parse(bytes.toString())["maps"]["baseColor"]["path"].toString();
        (*paths)[manifestPath] = path;
        return path;
    };
    sets.thumbnail = [this, baseColourOf](const juce::String& path) { return images.thumbnail ? images.thumbnail(baseColourOf(path)) : juce::Image(); };
    sets.image = [this, baseColourOf](const juce::String& path) { return images.image ? images.image(baseColourOf(path)) : juce::Image(); };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Open Surface Map";
    options.content.setOwned(new ProjectImagePicker(sets, {}, [this](const juce::String& manifestPath) {
        if (manifestPath.isEmpty())
            return;

        juce::MemoryBlock bytes;
        if (! projectSession->readEntry(manifestPath, bytes))
        {
            status("Could not read " + manifestPath + ".");
            return;
        }
        const auto manifest = juce::JSON::parse(bytes.toString());
        if (manifest["format"].toString() != texture_set::formatName)
        {
            status(manifestPath + " is not a texture set.");
            return;
        }

        settings = surface_maps::Settings::fromVar(manifest["settings"]);
        setSource(manifest["source"].toString());          // recomputes the maps with the saved settings
        openSetName = manifest["name"].toString();          // after setSource, which starts a new set on a new image
        settingsPanel->rebuild();
        status("Opened " + openSetName + ".");
    }));
    options.componentToCentreAround = mapsPanel->getTopLevelComponent();
    options.dialogBackgroundColour = juce::Colour(0xff161a1f);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;
    options.launchAsync();
}

void SurfaceMapWorkspace::newSurfaceMap()
{
    settings = {};
    setSource({});
    openSetName.clear();
    settingsPanel->rebuild();
    status("New surface map - choose a source image in Settings.");
}
