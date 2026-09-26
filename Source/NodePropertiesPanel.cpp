#include "NodePropertiesPanel.h"

namespace ns = ce::node_system;

namespace
{
const juce::Colour panelBackground { 0xff1e2227 };
constexpr int labelWidth = 110;
constexpr int rowGap = 6;

// "baseColor" -> "Base Color", "worldPositionOffset" -> "World Position Offset".
juce::String friendlyName(const std::string& name)
{
    juce::String out;
    for (size_t i = 0; i < name.size(); ++i)
    {
        const char c = name[i];
        if (i == 0)
            out << juce::String::charToString(static_cast<juce::juce_wchar>(std::toupper(static_cast<unsigned char>(c))));
        else if (std::isupper(static_cast<unsigned char>(c)))
            out << " " << juce::String::charToString(static_cast<juce::juce_wchar>(c));
        else if (c == '_')
            out << " ";
        else
            out << juce::String::charToString(static_cast<juce::juce_wchar>(c));
    }
    return out;
}

// Writes a new default value into the pin, looked up fresh each time so an editor never holds a stale pointer.
void setPinValue(ns::Graph* graph, ns::NodeId nodeId, ns::PinId pinId, ns::PinDefaultValue value)
{
    if (graph == nullptr)
        return;
    if (auto* node = graph->FindNode(nodeId))
        if (auto* pin = node->FindPin(pinId))
            pin->defaultValue = std::move(value);
}

std::unique_ptr<juce::Slider> makeNumberSlider(double value)
{
    auto slider = std::make_unique<juce::Slider>(juce::Slider::LinearBar, juce::Slider::TextBoxLeft);
    const double magnitude = std::abs(value);
    slider->setRange(juce::jmin(0.0, value - magnitude), juce::jmax(1.0, magnitude * 2.0), 0.0);
    slider->setNumDecimalPlacesToDisplay(3);
    slider->setValue(value, juce::dontSendNotification);
    return slider;
}

// A colour swatch; clicking it opens a colour picker anchored to the swatch (a button-triggered menu).
class ColourSwatch final : public juce::Component, private juce::ChangeListener
{
public:
    ColourSwatch(juce::Colour initial, std::function<void(juce::Colour)> onChange)
        : colour(initial), changed(std::move(onChange))
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        auto area = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.drawRoundedRectangle(area, 4.0f, 1.0f);
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        auto selector = std::make_unique<juce::ColourSelector>(juce::ColourSelector::showColourAtTop
                                                               | juce::ColourSelector::showSliders
                                                               | juce::ColourSelector::showColourspace);
        selector->setCurrentColour(colour, juce::dontSendNotification);
        selector->setSize(300, 320);
        selector->addChangeListener(this);
        juce::CallOutBox::launchAsynchronously(std::move(selector), getScreenBounds(), nullptr);
    }

private:
    void changeListenerCallback(juce::ChangeBroadcaster* source) override
    {
        if (auto* selector = dynamic_cast<juce::ColourSelector*>(source))
        {
            colour = selector->getCurrentColour();
            repaint();
            if (changed)
                changed(colour);
        }
    }

    juce::Colour colour;
    std::function<void(juce::Colour)> changed;
};

// Three sliders side by side for a vector value.
class VectorEditor final : public juce::Component
{
public:
    VectorEditor(ns::Vec3Default initial, std::function<void(ns::Vec3Default)> onChange)
        : value(initial), changed(std::move(onChange))
    {
        const float components[] = { value.x, value.y, value.z };
        for (int i = 0; i < 3; ++i)
        {
            auto slider = makeNumberSlider(components[i]);
            slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 20);
            slider->onValueChange = [this, i, s = slider.get()]() {
                const auto v = static_cast<float>(s->getValue());
                if (i == 0) value.x = v; else if (i == 1) value.y = v; else value.z = v;
                if (changed) changed(value);
            };
            addAndMakeVisible(*slider);
            sliders.push_back(std::move(slider));
        }
    }

    void resized() override
    {
        auto area = getLocalBounds();
        const int width = area.getWidth() / 3;
        for (auto& slider : sliders)
            slider->setBounds(area.removeFromLeft(width).reduced(1, 0));
    }

private:
    ns::Vec3Default value;
    std::function<void(ns::Vec3Default)> changed;
    std::vector<std::unique_ptr<juce::Slider>> sliders;
};
}

NodePropertiesPanel::NodePropertiesPanel()
{
    title.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(title);

    subtitle.setFont(juce::FontOptions(12.0f));
    subtitle.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(subtitle);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport);

    rebuild();
}

NodePropertiesPanel::~NodePropertiesPanel()
{
    rows.clear();
}

void NodePropertiesPanel::setHost(Host newHost)
{
    host = std::move(newHost);
    rebuild();
}

void NodePropertiesPanel::showNode(ns::NodeId id)
{
    nodeId = id;
    rebuild();
}

void NodePropertiesPanel::refresh()
{
    rebuild();
}

void NodePropertiesPanel::paint(juce::Graphics& g)
{
    g.fillAll(panelBackground);
}

void NodePropertiesPanel::resized()
{
    auto area = getLocalBounds().reduced(8);
    title.setBounds(area.removeFromTop(24));
    subtitle.setBounds(area.removeFromTop(18));
    area.removeFromTop(6);
    viewport.setBounds(area);
    layoutRows();
}

void NodePropertiesPanel::rebuild()
{
    rows.clear();
    content.removeAllChildren();

    auto* node = host.graph != nullptr && nodeId != 0 ? host.graph->FindNode(nodeId) : nullptr;
    if (node == nullptr)
    {
        nodeId = 0;
        title.setText("Properties", juce::dontSendNotification);
        subtitle.setText("Select a node to see and edit its values.", juce::dontSendNotification);
        layoutRows();
        return;
    }

    const auto* descriptor = host.registry != nullptr ? host.registry->Find(node->TypeName()) : nullptr;
    title.setText(descriptor != nullptr && ! descriptor->displayName.empty() ? juce::String(descriptor->displayName)
                                                                            : juce::String(node->TypeName()),
                  juce::dontSendNotification);
    subtitle.setText(descriptor != nullptr && ! descriptor->category.empty() ? juce::String(descriptor->category) + " node"
                                                                           : juce::String("Node"),
                     juce::dontSendNotification);

    for (const auto& pin : node->Inputs())
        addInputRow(*node, pin);

    juce::StringArray outputNames;
    for (const auto& pin : node->Outputs())
        outputNames.add(friendlyName(pin.name));
    if (! outputNames.isEmpty())
    {
        auto outputs = std::make_unique<juce::Label>();
        outputs->setText(outputNames.joinIntoString(", "), juce::dontSendNotification);
        outputs->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addRow("Outputs", std::move(outputs), 24);
    }

    layoutRows();
}

void NodePropertiesPanel::addInputRow(ns::Node& node, const ns::Pin& pin)
{
    if (pin.type.kind != ns::PinKind::Data)
        return;

    const auto labelText = friendlyName(pin.name);
    const auto connection = describeConnection(node, pin);
    if (connection.isNotEmpty())
    {
        auto info = std::make_unique<juce::Label>();
        info->setText(connection, juce::dontSendNotification);
        info->setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addRow(labelText, std::move(info), 24);
        return;
    }

    auto* graph = host.graph;
    const auto id = node.Id();
    const auto pinId = pin.id;
    const auto dataType = pin.type.dataType;

    // An image input: pick one of the project's images.
    if (dataType == ns::DataType::Texture)
    {
        const auto current = std::holds_alternative<std::string>(pin.defaultValue)
                               ? juce::String(std::get<std::string>(pin.defaultValue)) : juce::String();
        auto choices = host.listProjectImages ? host.listProjectImages() : juce::Array<ProjectImageList::ImageChoice>();
        auto list = std::make_unique<ProjectImageList>(choices, current, [this, graph, id, pinId](const juce::String& path) {
            setPinValue(graph, id, pinId, path.toStdString());
            valueEdited();
        });
        const int height = list->getPreferredHeight();
        addRow(labelText, std::move(list), height);
        return;
    }

    if (const auto* number = std::get_if<float>(&pin.defaultValue))
    {
        auto slider = makeNumberSlider(*number);
        slider->onValueChange = [this, graph, id, pinId, s = slider.get()]() {
            setPinValue(graph, id, pinId, static_cast<float>(s->getValue()));
            valueEdited();
        };
        addRow(labelText, std::move(slider), 26);
        return;
    }

    if (const auto* vec = std::get_if<ns::Vec3Default>(&pin.defaultValue))
    {
        if (dataType == ns::DataType::Color)
        {
            const auto initial = juce::Colour::fromFloatRGBA(vec->x, vec->y, vec->z, 1.0f);
            addRow(labelText, std::make_unique<ColourSwatch>(initial, [this, graph, id, pinId](juce::Colour c) {
                       setPinValue(graph, id, pinId, ns::Vec3Default { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() });
                       valueEdited();
                   }), 26);
        }
        else
        {
            addRow(labelText, std::make_unique<VectorEditor>(*vec, [this, graph, id, pinId](ns::Vec3Default v) {
                       setPinValue(graph, id, pinId, v);
                       valueEdited();
                   }), 26);
        }
        return;
    }

    if (const auto* flag = std::get_if<bool>(&pin.defaultValue))
    {
        auto toggle = std::make_unique<juce::ToggleButton>();
        toggle->setToggleState(*flag, juce::dontSendNotification);
        toggle->onClick = [this, graph, id, pinId, t = toggle.get()]() {
            setPinValue(graph, id, pinId, t->getToggleState());
            valueEdited();
        };
        addRow(labelText, std::move(toggle), 26);
        return;
    }

    if (const auto* integer = std::get_if<std::int64_t>(&pin.defaultValue))
    {
        auto slider = std::make_unique<juce::Slider>(juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft);
        slider->setRange(-1000000.0, 1000000.0, 1.0);
        slider->setValue(static_cast<double>(*integer), juce::dontSendNotification);
        slider->onValueChange = [this, graph, id, pinId, s = slider.get()]() {
            setPinValue(graph, id, pinId, static_cast<std::int64_t>(s->getValue()));
            valueEdited();
        };
        addRow(labelText, std::move(slider), 26);
        return;
    }

    if (const auto* text = std::get_if<std::string>(&pin.defaultValue))
    {
        auto editor = std::make_unique<juce::TextEditor>();
        editor->setText(juce::String(*text), juce::dontSendNotification);
        auto commit = [this, graph, id, pinId, e = editor.get()]() {
            setPinValue(graph, id, pinId, e->getText().toStdString());
            valueEdited();
        };
        editor->onReturnKey = commit;
        editor->onFocusLost = commit;
        addRow(labelText, std::move(editor), 26);
        return;
    }

    // No default value: this input only takes a value through a wire (for example UV, which falls back to the
    // mesh's own coordinates).
    auto info = std::make_unique<juce::Label>();
    info->setText("Not connected - uses its built-in default", juce::dontSendNotification);
    info->setColour(juce::Label::textColourId, juce::Colours::grey);
    addRow(labelText, std::move(info), 24);
}

void NodePropertiesPanel::addRow(const juce::String& labelText, std::unique_ptr<juce::Component> editor, int height)
{
    Row row;
    row.label = std::make_unique<juce::Label>();
    row.label->setText(labelText, juce::dontSendNotification);
    row.label->setColour(juce::Label::textColourId, juce::Colours::white);
    row.label->setJustificationType(juce::Justification::topLeft);
    row.editor = std::move(editor);
    row.height = height;
    content.addAndMakeVisible(*row.label);
    content.addAndMakeVisible(*row.editor);
    rows.push_back(std::move(row));
}

void NodePropertiesPanel::layoutRows()
{
    const int width = juce::jmax(200, viewport.getWidth() - viewport.getScrollBarThickness());
    int y = 0;
    for (auto& row : rows)
    {
        row.label->setBounds(0, y, labelWidth, juce::jmin(row.height, 26));
        row.editor->setBounds(labelWidth, y, width - labelWidth, row.height);
        y += row.height + rowGap;
    }
    content.setSize(width, juce::jmax(y, 1));
}

void NodePropertiesPanel::valueEdited()
{
    if (host.onValueEdited)
        host.onValueEdited();
}

juce::String NodePropertiesPanel::describeConnection(const ns::Node& node, const ns::Pin& pin) const
{
    if (host.graph == nullptr)
        return {};

    for (const auto& connection : host.graph->Connections())
    {
        if (connection.toNode != node.Id() || connection.toPin != pin.id)
            continue;

        const auto* from = host.graph->FindNode(connection.fromNode);
        if (from == nullptr)
            return "Connected";

        const auto* descriptor = host.registry != nullptr ? host.registry->Find(from->TypeName()) : nullptr;
        const juce::String fromName = descriptor != nullptr && ! descriptor->displayName.empty()
                                        ? juce::String(descriptor->displayName) : juce::String(from->TypeName());
        const auto* fromPin = from->FindPin(connection.fromPin);
        return "Wired from " + fromName + (fromPin != nullptr ? " (" + friendlyName(fromPin->name) + ")" : juce::String());
    }
    return {};
}
