// The Virtual Engineer's tools in the Graph editor (shared/VirtualEngineer, docs/architecture/Suite-Agent-Runtime-Spec.md
// section 5): read the graph and the node types, add, wire, set and remove nodes, and check the result. Each tool does
// what the editor does - the same node-system calls, then graphEdited() - so the agent has no back door and the graph
// it leaves is one the user could have made.

#include "GraphWorkspace.h"

#include <creation/material/material_compiler.h>
#include <node_system/frgraph_serialization.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>

namespace ns = ce::node_system;
namespace agent = creation::agent;

namespace
{
juce::var schema(const char* json)
{
    return juce::JSON::parse(juce::String(json));
}

juce::String dataTypeName(ns::DataType type)
{
    switch (type)
    {
        case ns::DataType::Any: return "any";
        case ns::DataType::Float: return "float";
        case ns::DataType::Vec2: return "vec2";
        case ns::DataType::Vec3: return "vec3";
        case ns::DataType::Vec4: return "vec4";
        case ns::DataType::Color: return "color";
        case ns::DataType::Bool: return "bool";
        case ns::DataType::Int: return "int";
        case ns::DataType::String: return "string";
        case ns::DataType::Transform: return "transform";
        case ns::DataType::BoneTransform: return "bone transform";
        case ns::DataType::Texture: return "image";
        case ns::DataType::AudioSignal: return "audio";
        case ns::DataType::Entity: return "entity";
        case ns::DataType::Function: return "function";
        case ns::DataType::Material: return "material";
        case ns::DataType::Model: return "model";
        case ns::DataType::Controller: return "controller";
        case ns::DataType::Drawing: return "drawing";
        case ns::DataType::Brush: return "brush";
        case ns::DataType::Struct: return "struct";
    }
    return "any";
}

juce::var vec3(const ns::Vec3Default& v)
{
    juce::Array<juce::var> list { v.x, v.y, v.z };
    return juce::var(list);
}

agent::ToolResult noNode(int id)
{
    return agent::ToolResult::failure("unknown_node", "There is no node " + juce::String(id) + " in the graph.",
                                      "Call texture.graph.get to see the nodes and their ids.");
}

// A pin named by its name (or its number) among a node's inputs or outputs.
const ns::Pin* findPin(const ns::Node& node, const juce::var& ref, bool input)
{
    const auto& pins = input ? node.Inputs() : node.Outputs();
    for (const auto& pin : pins)
        if (ref.isString() ? juce::String(pin.name).equalsIgnoreCase(ref.toString()) : static_cast<juce::int64>(pin.id) == static_cast<juce::int64>(ref))
            return &pin;
    return nullptr;
}

juce::String pinNames(const ns::Node& node, bool input)
{
    juce::StringArray names;
    for (const auto& pin : input ? node.Inputs() : node.Outputs())
        names.add(pin.name);
    return names.isEmpty() ? juce::String("none") : names.joinIntoString(", ");
}

int nodeArg(const juce::var& args, const char* name)
{
    return static_cast<int>(args.getProperty(name, 0));
}
} // namespace

juce::String GraphWorkspace::pinTypeForAgent(const ns::Node& node, const ns::Pin& pin) const
{
    if (pin.type.dataType == ns::DataType::Struct)
        return "struct " + juce::String(pin.type.structType);
    if (const auto* def = ns::PinEnum(graph, registry, node, pin))
    {
        juce::StringArray choices;
        for (const auto& variant : def->variants)
            choices.add(variant.name);
        return "enum " + juce::String(def->name) + " (" + choices.joinIntoString(", ") + ")";
    }
    const auto name = dataTypeName(pin.type.dataType);
    return pin.type.dataType == ns::DataType::Color ? name + " [r, g, b] from 0 to 1" : name;
}

juce::var GraphWorkspace::pinValueForAgent(const ns::Node& node, const ns::Pin& pin) const
{
    const auto& value = pin.defaultValue;
    if (const auto* f = std::get_if<float>(&value))
        return *f;
    if (const auto* i = std::get_if<std::int64_t>(&value))
    {
        if (const auto* def = ns::PinEnum(graph, registry, node, pin); def != nullptr && *i >= 0 && *i < static_cast<std::int64_t>(def->variants.size()))
            return juce::String(def->variants[static_cast<size_t>(*i)].name);
        return static_cast<juce::int64>(*i);
    }
    if (const auto* b = std::get_if<bool>(&value))
        return *b;
    if (const auto* s = std::get_if<std::string>(&value))
        return juce::String(*s);
    if (const auto* v = std::get_if<ns::Vec3Default>(&value))
        return vec3(*v);
    return {};
}

juce::var GraphWorkspace::nodeForAgent(const ns::Node& node) const
{
    auto* n = new juce::DynamicObject();
    n->setProperty("id", static_cast<int>(node.Id()));
    n->setProperty("type", juce::String(node.TypeName()));
    if (const auto* d = registry.Find(node.TypeName()))
        n->setProperty("title", juce::String(d->displayName));
    n->setProperty("x", node.EditorX());
    n->setProperty("y", node.EditorY());

    juce::Array<juce::var> inputs, outputs;
    for (const auto& pin : node.Inputs())
    {
        auto* p = new juce::DynamicObject();
        p->setProperty("id", static_cast<int>(pin.id));
        p->setProperty("name", juce::String(pin.name));
        p->setProperty("type", pinTypeForAgent(node, pin));
        bool wired = false;
        for (const auto& wire : graph.Connections())
            if (wire.toNode == node.Id() && wire.toPin == pin.id)
            {
                auto* from = new juce::DynamicObject();
                from->setProperty("node", static_cast<int>(wire.fromNode));
                if (const auto* source = graph.FindNode(wire.fromNode))
                    if (const auto* sourcePin = source->FindPin(wire.fromPin))
                        from->setProperty("pin", juce::String(sourcePin->name));
                p->setProperty("wiredFrom", juce::var(from));
                wired = true;
            }
        if (! wired)
            if (const auto value = pinValueForAgent(node, pin); ! value.isVoid())
                p->setProperty("value", value);
        inputs.add(juce::var(p));
    }
    for (const auto& pin : node.Outputs())
    {
        auto* p = new juce::DynamicObject();
        p->setProperty("id", static_cast<int>(pin.id));
        p->setProperty("name", juce::String(pin.name));
        p->setProperty("type", pinTypeForAgent(node, pin));
        outputs.add(juce::var(p));
    }
    n->setProperty("inputs", juce::var(inputs));
    n->setProperty("outputs", juce::var(outputs));
    if (auto found = errors.find(node.Id()); found != errors.end())
        n->setProperty("error", found->second);
    return juce::var(n);
}

juce::var GraphWorkspace::graphStateForAgent() const
{
    auto* body = new juce::DynamicObject();
    body->setProperty("kind", isMaterial() ? "material" : "image");
    body->setProperty("name", graphName);
    body->setProperty("unsavedChanges", edited);
    body->setProperty("selectedNode", static_cast<int>(selectedNode));
    juce::Array<juce::var> nodes, wires;
    for (const auto& [id, node] : graph.Nodes())
        nodes.add(nodeForAgent(*node));
    for (const auto& wire : graph.Connections())
    {
        auto* w = new juce::DynamicObject();
        w->setProperty("id", static_cast<juce::int64>(wire.id));
        auto end = [this](ns::NodeId nodeId, ns::PinId pinId) {
            auto* e = new juce::DynamicObject();
            e->setProperty("node", static_cast<int>(nodeId));
            if (const auto* node = graph.FindNode(nodeId))
                if (const auto* pin = node->FindPin(pinId))
                    e->setProperty("pin", juce::String(pin->name));
            return juce::var(e);
        };
        w->setProperty("from", end(wire.fromNode, wire.fromPin));
        w->setProperty("to", end(wire.toNode, wire.toPin));
        wires.add(juce::var(w));
    }
    body->setProperty("nodes", juce::var(nodes));
    body->setProperty("wires", juce::var(wires));
    return juce::var(body);
}

void GraphWorkspace::whenEvaluated(std::function<void()> done)
{
    if (isMaterial())
        return done(); // a material is compiled where it is edited, at once
    const auto current = ns::SerializeGraph(graph);
    if (current == lastEvaluatedGraph)
        return done();
    evaluationWaiters.push_back({ current, std::move(done) });
    requestEvaluation();
}

void GraphWorkspace::evaluationFinished(const std::string& graphText)
{
    lastEvaluatedGraph = graphText;
    auto waiting = std::move(evaluationWaiters);
    evaluationWaiters.clear();
    for (auto& [text, done] : waiting)
        if (text == graphText)
            done();
        else
            evaluationWaiters.push_back({ text, std::move(done) }); // evaluated an older graph; theirs is still coming
}

void GraphWorkspace::registerAgentTools(agent::VirtualEngineer& engineer)
{
    using Done = std::function<void(agent::ToolResult)>;
    auto add = [&engineer](agent::ToolDefinition definition, agent::ToolHandler handler) {
        juce::String error;
        if (! engineer.addTool(std::move(definition), std::move(handler), error))
            jassertfalse; // a definition here is wrong: error says how
    };

    // The graph is the state these tools change: a request's changes undo together (spec section 8).
    engineer.addStateDomain({ "graph",
                              [this] {
                                  auto* state = new juce::DynamicObject();
                                  state->setProperty("graph", juce::String(ns::SerializeGraph(graph)));
                                  state->setProperty("name", graphName);
                                  state->setProperty("edited", edited);
                                  return juce::var(state);
                              },
                              [this](const juce::var& state) {
                                  std::string error;
                                  auto restored = ns::DeserializeGraph(state.getProperty("graph", {}).toString().toStdString(), error);
                                  if (restored == nullptr)
                                      return;
                                  adoptGraph(std::move(*restored), state.getProperty("name", {}).toString());
                                  edited = static_cast<bool>(state.getProperty("edited", false));
                                  status("Undid the Virtual Engineer's last request.");
                              } });

    add({ "texture.graph.get", "Read the graph",
          "The open graph: every node (id, type, title, position, its inputs with their types and values or the wire feeding "
          "them, its outputs, its error if it fails) and every wire. Read it before changing the graph and after, to check.",
          schema(R"({"type":"object","properties":{},"additionalProperties":false})"), agent::Effect::read },
        [this](const juce::var&, Done done) { done(agent::ToolResult::success(graphStateForAgent())); });

    add({ "texture.nodes.types", "Find node types",
          "The node types that can go in this graph, with their inputs and outputs. Give `search` (a word such as \"blur\" or "
          "\"noise\") to narrow the list; it matches type, title, category and description.",
          schema(R"({"type":"object","properties":{"search":{"type":"string","maxLength":60}},"additionalProperties":false})"),
          agent::Effect::read },
        [this](const juce::var& args, Done done) {
            const auto search = args.getProperty("search", {}).toString().trim().toLowerCase();
            std::vector<const ns::NodeTypeDescriptor*> found;
            for (const auto& [name, d] : registry.Types())
            {
                if (! ns::AllowedInDiagram(d, graph.DiagramType()))
                    continue;
                const auto words = (juce::String(d.typeName) + " " + d.displayName + " " + d.category + " " + d.description).toLowerCase();
                if (search.isEmpty() || words.contains(search))
                    found.push_back(&d);
            }
            std::sort(found.begin(), found.end(), [](const auto* a, const auto* b) {
                return std::tie(a->category, a->displayName) < std::tie(b->category, b->displayName);
            });
            juce::Array<juce::var> list;
            for (const auto* d : found)
            {
                if (list.size() >= 60)
                    break;
                auto* t = new juce::DynamicObject();
                t->setProperty("type", juce::String(d->typeName));
                t->setProperty("title", juce::String(d->displayName));
                t->setProperty("category", juce::String(d->category));
                if (! d->description.empty())
                    t->setProperty("description", juce::String(d->description).substring(0, 200));
                juce::StringArray ins, outs;
                for (const auto& pin : d->inputs)
                    ins.add(juce::String(pin.name) + ": " + dataTypeName(pin.type.dataType));
                for (const auto& pin : d->outputs)
                    outs.add(juce::String(pin.name) + ": " + dataTypeName(pin.type.dataType));
                t->setProperty("inputs", ins.joinIntoString(", "));
                t->setProperty("outputs", outs.joinIntoString(", "));
                list.add(juce::var(t));
            }
            auto* body = new juce::DynamicObject();
            body->setProperty("types", juce::var(list));
            body->setProperty("matching", static_cast<int>(found.size()));
            if (static_cast<int>(found.size()) > list.size())
                body->setProperty("note", "Only the first 60 are listed; narrow the search.");
            done(agent::ToolResult::success(juce::var(body)));
        });

    add({ "texture.graph.add_node", "Add a node",
          "Adds a node of a type from texture.nodes.types. x and y place it (the graph's own coordinates; nodes are about 200 "
          "wide); without them it goes to the right of the others. Returns the new node with its pins.",
          schema(R"({"type":"object","properties":{"type":{"type":"string","maxLength":120},"x":{"type":"number"},"y":{"type":"number"}},
                    "required":["type"],"additionalProperties":false})"),
          agent::Effect::write },
        [this](const juce::var& args, Done done) {
            const auto type = args.getProperty("type", {}).toString().toStdString();
            const auto* d = registry.Find(type);
            if (d == nullptr)
                return done(agent::ToolResult::failure("unknown_type", "There is no node type \"" + juce::String(type) + "\".",
                                                       "Call texture.nodes.types with a search word to find the type's exact name."));
            if (! ns::AllowedInDiagram(*d, graph.DiagramType()))
                return done(agent::ToolResult::failure("wrong_graph", juce::String(d->displayName) + " does not belong in a "
                                                                          + (isMaterial() ? "material" : "image") + " graph."));
            std::string error;
            auto* node = ns::AddRegisteredNode(graph, registry, type, &error);
            if (node == nullptr)
                return done(agent::ToolResult::failure("not_added", juce::String(error)));
            float x = 0.0f, y = 0.0f;
            if (args.hasProperty("x") || args.hasProperty("y"))
            {
                x = static_cast<float>(static_cast<double>(args.getProperty("x", 0.0)));
                y = static_cast<float>(static_cast<double>(args.getProperty("y", 0.0)));
            }
            else
            {
                bool any = false;
                for (const auto& [id, other] : graph.Nodes())
                    if (id != node->Id())
                    {
                        x = any ? juce::jmax(x, other->EditorX() + 260.0f) : other->EditorX() + 260.0f;
                        y = any ? y : other->EditorY();
                        any = true;
                    }
            }
            node->SetEditorPosition(x, y);
            const auto id = node->Id();
            graphEdited();
            const auto* added = graph.FindNode(id);
            done(agent::ToolResult::success(nodeForAgent(*added), { "added " + juce::String(d->displayName) + " (node " + juce::String(static_cast<int>(id)) + ")" }));
        });

    add({ "texture.graph.connect", "Wire two nodes",
          "Wires an output of one node to an input of another. Pins are named by name (or id) as texture.graph.get shows them. "
          "An input takes one wire: a wire already into it is replaced. The types must fit.",
          schema(R"({"type":"object","properties":{"fromNode":{"type":"integer"},"fromPin":{"type":["string","integer"]},
                    "toNode":{"type":"integer"},"toPin":{"type":["string","integer"]}},
                    "required":["fromNode","fromPin","toNode","toPin"],"additionalProperties":false})"),
          agent::Effect::write },
        [this](const juce::var& args, Done done) {
            const auto* from = graph.FindNode(static_cast<ns::NodeId>(nodeArg(args, "fromNode")));
            const auto* to = graph.FindNode(static_cast<ns::NodeId>(nodeArg(args, "toNode")));
            if (from == nullptr)
                return done(noNode(nodeArg(args, "fromNode")));
            if (to == nullptr)
                return done(noNode(nodeArg(args, "toNode")));
            const auto* out = findPin(*from, args.getProperty("fromPin", {}), false);
            const auto* in = findPin(*to, args.getProperty("toPin", {}), true);
            if (out == nullptr)
                return done(agent::ToolResult::failure("unknown_pin", "Node " + juce::String(nodeArg(args, "fromNode")) + " has no output \""
                                                                          + args.getProperty("fromPin", {}).toString() + "\"; its outputs: " + pinNames(*from, false) + "."));
            if (in == nullptr)
                return done(agent::ToolResult::failure("unknown_pin", "Node " + juce::String(nodeArg(args, "toNode")) + " has no input \""
                                                                          + args.getProperty("toPin", {}).toString() + "\"; its inputs: " + pinNames(*to, true) + "."));
            const auto fromId = from->Id(), toId = to->Id();
            const auto outId = out->id, inId = in->id;
            const auto outName = juce::String(out->name), inName = juce::String(in->name);
            const auto outType = pinTypeForAgent(*from, *out), inType = pinTypeForAgent(*to, *in);

            // An input takes one wire: the old one goes, and comes back if the new one is refused.
            std::optional<ns::Connection> previous;
            for (const auto& wire : graph.Connections())
                if (wire.toNode == toId && wire.toPin == inId)
                    previous = wire;
            if (previous)
                graph.Disconnect(previous->id);
            ns::ConnectError why {};
            const auto made = graph.Connect(fromId, outId, toId, inId, &why);
            if (! made)
            {
                if (previous)
                    graph.ConnectWithId(previous->id, previous->fromNode, previous->fromPin, previous->toNode, previous->toPin);
                const auto reason = why == ns::ConnectError::IncompatibleTypes
                                        ? "the output is " + outType + " and the input takes " + inType
                                        : juce::String("the pins cannot be wired that way (an output to an input)");
                return done(agent::ToolResult::failure("cannot_connect", "Cannot wire " + outName + " to " + inName + ": " + reason + ".",
                                                       "Choose pins whose types fit, or put a converting node between them."));
            }
            graphEdited();
            juce::String change;
            change << "wired node " << static_cast<int>(fromId) << " " << outName << " to node " << static_cast<int>(toId) << " " << inName;
            if (previous)
                change << " (replacing the wire from node " << static_cast<int>(previous->fromNode) << ")";
            auto* data = new juce::DynamicObject();
            data->setProperty("wire", static_cast<juce::int64>(*made));
            done(agent::ToolResult::success(juce::var(data), { change }));
        });

    add({ "texture.graph.disconnect", "Remove a wire",
          "Removes one wire, by its id from texture.graph.get, or every wire into or out of one pin (node and pin).",
          schema(R"({"type":"object","properties":{"wire":{"type":"integer"},"node":{"type":"integer"},"pin":{"type":["string","integer"]}},
                    "additionalProperties":false})"),
          agent::Effect::write },
        [this](const juce::var& args, Done done) {
            if (args.hasProperty("wire"))
            {
                const auto id = static_cast<ns::ConnectionId>(static_cast<juce::int64>(args.getProperty("wire", 0)));
                if (! graph.Disconnect(id))
                    return done(agent::ToolResult::failure("unknown_wire", "There is no wire " + args.getProperty("wire", {}).toString() + ".",
                                                           "Call texture.graph.get to see the wires and their ids."));
                graphEdited();
                return done(agent::ToolResult::success({}, { "removed wire " + args.getProperty("wire", {}).toString() }));
            }
            const auto* node = graph.FindNode(static_cast<ns::NodeId>(nodeArg(args, "node")));
            if (node == nullptr)
                return done(noNode(nodeArg(args, "node")));
            const auto* pin = findPin(*node, args.getProperty("pin", {}), true);
            if (pin == nullptr)
                pin = findPin(*node, args.getProperty("pin", {}), false);
            if (pin == nullptr)
                return done(agent::ToolResult::failure("unknown_pin", "Node " + juce::String(static_cast<int>(node->Id())) + " has no pin \""
                                                                          + args.getProperty("pin", {}).toString() + "\"."));
            const auto pinName = juce::String(pin->name);
            graph.DisconnectPin(node->Id(), pin->id);
            graphEdited();
            done(agent::ToolResult::success({}, { "removed the wires of node " + juce::String(static_cast<int>(node->Id())) + " " + pinName }));
        });

    add({ "texture.graph.set_input", "Set an input's value",
          "Sets the value of an input that is not wired: a number, a whole number, true/false, text, an image's project path, "
          "a colour as [r, g, b] from 0 to 1, a vector as [x, y, z], or an enum choice by name. Returns the node.",
          schema(R"({"type":"object","properties":{"node":{"type":"integer"},"pin":{"type":["string","integer"]},
                    "value":{"type":["number","integer","boolean","string","array"]}},
                    "required":["node","pin","value"],"additionalProperties":false})"),
          agent::Effect::write },
        [this](const juce::var& args, Done done) {
            auto* node = graph.FindNode(static_cast<ns::NodeId>(nodeArg(args, "node")));
            if (node == nullptr)
                return done(noNode(nodeArg(args, "node")));
            const auto* found = findPin(*node, args.getProperty("pin", {}), true);
            if (found == nullptr)
                return done(agent::ToolResult::failure("unknown_pin", "Node " + juce::String(nodeArg(args, "node")) + " has no input \""
                                                                          + args.getProperty("pin", {}).toString() + "\"; its inputs: " + pinNames(*node, true) + "."));
            for (const auto& wire : graph.Connections())
                if (wire.toNode == node->Id() && wire.toPin == found->id)
                    return done(agent::ToolResult::failure("wired", juce::String(found->name) + " is wired; its value comes from the wire.",
                                                           "Remove the wire with texture.graph.disconnect first, or change the node it comes from."));
            auto* pin = node->FindPin(found->id);
            const auto value = args.getProperty("value", {});
            const auto type = pinTypeForAgent(*node, *pin);
            auto wrong = [&](const juce::String& wanted) {
                return agent::ToolResult::failure("wrong_type", juce::String(pin->name) + " takes " + wanted + ".", "Its type is " + type + ".");
            };

            if (const auto* def = ns::PinEnum(graph, registry, *node, *pin))
            {
                std::int64_t index = -1;
                if (value.isString())
                {
                    for (size_t i = 0; i < def->variants.size(); ++i)
                        if (juce::String(def->variants[i].name).equalsIgnoreCase(value.toString()))
                            index = static_cast<std::int64_t>(i);
                }
                else if (value.isInt() || value.isInt64())
                    index = static_cast<juce::int64>(value);
                if (index < 0 || index >= static_cast<std::int64_t>(def->variants.size()))
                    return done(wrong("one of its choices"));
                pin->defaultValue = index;
            }
            else if (std::holds_alternative<float>(pin->defaultValue))
            {
                if (! (value.isDouble() || value.isInt() || value.isInt64()))
                    return done(wrong("a number"));
                pin->defaultValue = static_cast<float>(static_cast<double>(value));
            }
            else if (std::holds_alternative<std::int64_t>(pin->defaultValue))
            {
                if (! (value.isInt() || value.isInt64() || (value.isDouble() && static_cast<double>(value) == std::floor(static_cast<double>(value)))))
                    return done(wrong("a whole number"));
                pin->defaultValue = static_cast<std::int64_t>(static_cast<double>(value));
            }
            else if (std::holds_alternative<bool>(pin->defaultValue))
            {
                if (! value.isBool())
                    return done(wrong("true or false"));
                pin->defaultValue = static_cast<bool>(value);
            }
            else if (std::holds_alternative<std::string>(pin->defaultValue))
            {
                if (! value.isString())
                    return done(wrong(pin->type.dataType == ns::DataType::Texture ? "an image's project path" : "text"));
                pin->defaultValue = value.toString().toStdString();
            }
            else if (std::holds_alternative<ns::Vec3Default>(pin->defaultValue))
            {
                const auto* list = value.getArray();
                if (list == nullptr || list->size() != 3)
                    return done(wrong("three numbers, [x, y, z]"));
                ns::Vec3Default v { static_cast<float>(static_cast<double>(list->getReference(0))), static_cast<float>(static_cast<double>(list->getReference(1))),
                                    static_cast<float>(static_cast<double>(list->getReference(2))) };
                if (pin->type.dataType == ns::DataType::Color && (v.x < 0 || v.x > 1 || v.y < 0 || v.y > 1 || v.z < 0 || v.z > 1))
                    return done(wrong("a colour, each of r, g and b from 0 to 1"));
                pin->defaultValue = v;
            }
            else
                return done(agent::ToolResult::failure("not_settable", juce::String(pin->name) + " has no value of its own; it only takes a wire.",
                                                       "Wire a node into it with texture.graph.connect."));

            const auto id = node->Id();
            const auto pinName = juce::String(pin->name);
            const auto shown = juce::JSON::toString(pinValueForAgent(*node, *pin), true);
            graphEdited();
            properties.refresh();
            done(agent::ToolResult::success(nodeForAgent(*graph.FindNode(id)),
                                            { "set node " + juce::String(static_cast<int>(id)) + " " + pinName + " to " + shown }));
        });

    add({ "texture.graph.remove_node", "Remove a node",
          "Removes a node and its wires. The user is asked first.",
          schema(R"({"type":"object","properties":{"node":{"type":"integer"}},"required":["node"],"additionalProperties":false})"),
          agent::Effect::destructive },
        [this](const juce::var& args, Done done) {
            const auto id = static_cast<ns::NodeId>(nodeArg(args, "node"));
            const auto* node = graph.FindNode(id);
            if (node == nullptr)
                return done(noNode(nodeArg(args, "node")));
            const auto* d = registry.Find(node->TypeName());
            const auto title = d != nullptr ? juce::String(d->displayName) : juce::String(node->TypeName());
            if (selectedNode == id)
                graphView.ClearSelection();
            graph.RemoveNode(id);
            graphEdited();
            done(agent::ToolResult::success({}, { "removed " + title + " (node " + juce::String(static_cast<int>(id)) + ")" }));
        });

    add({ "texture.graph.check", "Check the graph",
          "Waits until the graph as it is now has been worked out, then returns the nodes that fail with their errors (an image "
          "graph is evaluated, a material compiled). Use it after changing the graph to check the result.",
          schema(R"({"type":"object","properties":{},"additionalProperties":false})"), agent::Effect::read },
        [this](const juce::var&, Done done) {
            whenEvaluated([this, done] {
                auto* body = new juce::DynamicObject();
                juce::Array<juce::var> failing;
                if (isMaterial())
                {
                    const auto compiled = ce::material::CompileMaterialGraph(graph, registry);
                    for (const auto& error : compiled.errors)
                        failing.add(juce::String(error));
                }
                else
                    failing = *errorsForAgent().getArray();
                body->setProperty("ok", failing.isEmpty());
                body->setProperty("errors", juce::var(failing));
                body->setProperty("nodes", static_cast<int>(graph.Nodes().size()));
                done(agent::ToolResult::success(juce::var(body)));
            });
        });
}
