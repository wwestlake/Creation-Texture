#include "TextureCompiler.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace creation_texture {
namespace {
namespace ns = ce::node_system;

const ns::Pin* Input(const ns::Node& node, const std::string& name)
{
    const auto it = std::find_if(node.Inputs().begin(), node.Inputs().end(), [&](const ns::Pin& pin) { return pin.name == name; });
    return it == node.Inputs().end() ? nullptr : &*it;
}

const ns::Connection* Incoming(const ns::Graph& graph, ns::NodeId nodeId, ns::PinId pinId)
{
    const auto it = std::find_if(graph.Connections().begin(), graph.Connections().end(),
                                 [&](const ns::Connection& c) { return c.toNode == nodeId && c.toPin == pinId; });
    return it == graph.Connections().end() ? nullptr : &*it;
}

std::string FormatGlslFloat(float number)
{
    std::ostringstream out;
    out << number;
    std::string text = out.str();
    if (text.find('.') == std::string::npos && text.find('e') == std::string::npos && text.find('E') == std::string::npos)
        text += ".0";
    return text + "f";
}

} // namespace

TextureCompileResult CompileTextureGraph(const ns::Graph& graph, const ns::NodeTypeRegistry& registry)
{
    TextureCompileResult result;
    std::unordered_map<ns::NodeId, std::string> cache;
    std::unordered_map<ns::NodeId, bool> visiting;

    std::function<std::string(ns::NodeId, ns::PinId)> emit = [&](ns::NodeId nodeId, ns::PinId outputPin) -> std::string {
        const auto* node = graph.FindNode(nodeId);
        if (node == nullptr) { result.errors.push_back("Graph references missing node."); return "vec4(0.0f)"; }
        if (visiting[nodeId]) { result.errors.push_back("Cycle detected at node " + std::to_string(nodeId) + "."); return "vec4(0.0f)"; }
        if (const auto cached = cache.find(nodeId); cached != cache.end()) return cached->second;
        
        visiting[nodeId] = true;
        std::string expression;
        
        auto inputExpression = [&](const char* name, const std::string& fallback) {
            const auto* pin = Input(*node, name);
            if (pin == nullptr) return fallback;
            if (const auto* connection = Incoming(graph, nodeId, pin->id)) return emit(connection->fromNode, connection->fromPin);
            return fallback;
        };
        
        const auto& type = node->TypeName();
        
        if (type == "texture.imageInput") {
            expression = "vec4(vUV.x, vUV.y, 0.0f, 1.0f)";
        } else if (type == "texture.imageOutput") {
            result.errors.push_back("Output is not a value expression.");
        } else if (type == "texture.makeTileable") {
            expression = "( " + inputExpression("image", "vec4(0.0f)") + " * " + inputExpression("blendSoftness", "0.5f") + ")";
        } else if (type == "texture.tileSampler") {
            expression = "( " + inputExpression("pattern", "vec4(0.0f)") + " * " + inputExpression("scale", "1.0f") + ")";
        } else if (type == "texture.directionalWarp") {
            expression = "( " + inputExpression("image", "vec4(0.0f)") + " + " + inputExpression("intensity", "1.0f") + " * " + inputExpression("angle", "0.0f") + ")";
        } else if (type == "texture.contrastAdjustment") {
            expression = "vec4(pow(" + inputExpression("image", "vec4(0.0f)") + ".rgb, vec3(1.0 + " + inputExpression("contrast", "0.0f") + ")), 1.0)";
        } else {
            result.errors.push_back("Unsupported node: " + type);
        }
        
        visiting[nodeId] = false;
        cache[nodeId] = expression;
        return expression;
    };

    const ns::Node* output = nullptr;
    for (const auto& [id, node] : graph.Nodes()) {
        if (node->TypeName() == "texture.imageOutput") {
            if (output != nullptr) result.errors.push_back("Graph must contain exactly one Image Output node.");
            output = node.get();
        }
    }
    if (output == nullptr) result.errors.push_back("Graph is missing an Image Output node.");
    if (!result.errors.empty()) return result;

    const auto value = [&](const char* name, const std::string& fallback) {
        const auto* pin = Input(*output, name);
        if (pin == nullptr) return fallback;
        if (const auto* connection = Incoming(graph, output->Id(), pin->id)) return emit(connection->fromNode, connection->fromPin);
        return fallback;
    };
    
    const std::string finalColor = value("image", "vec4(0.0f, 0.0f, 0.0f, 1.0f)");
    
    if (!result.errors.empty()) return result;

    std::ostringstream function;
    function << "void EvaluateTexture(in vec2 vUV, out vec4 outColor) {\n"
             << "    outColor = " << finalColor << ";\n"
             << "}";
             
    result.source.evaluateFunction = function.str();
    result.ok = true;
    return result;
}

} // namespace creation_texture
