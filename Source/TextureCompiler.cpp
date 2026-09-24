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


    static const std::unordered_map<std::string, std::string> kUnaryFloatFn = {
        {"texture.sin", "sin"}, {"texture.cos", "cos"}, {"texture.tan", "tan"},
        {"texture.asin", "asin"}, {"texture.acos", "acos"}, {"texture.atan", "atan"},
        {"texture.exp", "exp"}, {"texture.exp2", "exp2"}, {"texture.log", "log"}, {"texture.log2", "log2"},
        {"texture.sqrt", "sqrt"}, {"texture.inversesqrt", "inversesqrt"},
        {"texture.abs", "abs"}, {"texture.sign", "sign"}, {"texture.floor", "floor"},
        {"texture.ceil", "ceil"}, {"texture.fract", "fract"}
    };
TextureCompileResult CompileTextureGraph(const ns::Graph& graph, const ns::NodeTypeRegistry& registry)
{
    TextureCompileResult result;
    std::unordered_map<ns::NodeId, std::string> cache;
    std::unordered_map<ns::NodeId, bool> visiting;
    bool usesRotator = false;
    bool usesNormalFromHeight = false;
    std::unordered_map<std::string, TextureParameter> parameters;
    std::unordered_map<std::string, std::string> textureSlots;

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
        } else if (type == "texture.power") {
            expression = "pow(" + inputExpression("base", "1.0f") + ", " + inputExpression("exponent", "1.0f") + ")";
        } else if (type == "texture.fmod") {
            expression = "mod(" + inputExpression("a", "0.0f") + ", " + inputExpression("b", "1.0f") + ")";
        } else if (type == "texture.clamp") {
            expression = "clamp(" + inputExpression("value", "0.0f") + ", " + inputExpression("min", "0.0f") + ", " + inputExpression("max", "1.0f") + ")";
        } else if (type == "texture.lerp") {
            expression = "mix(" + inputExpression("a", "0.0f") + ", " + inputExpression("b", "1.0f") + ", " + inputExpression("alpha", "0.5f") + ")";
        } else if (type == "texture.oneminus") {
            expression = "(1.0f - " + inputExpression("value", "0.0f") + ")";
        } else if (type == "texture.if") {
            expression = "(" + inputExpression("a", "0.0f") + " > " + inputExpression("b", "0.0f") + " ? " + inputExpression("aGreaterThanB", "1.0f")
                       + " : (" + inputExpression("a", "0.0f") + " == " + inputExpression("b", "0.0f") + " ? " + inputExpression("aEqualsB", "0.0f")
                       + " : " + inputExpression("aLessThanB", "0.0f") + "))";
        } else if (type == "texture.append") {
            expression = "vec3(" + inputExpression("x", "0.0f") + ", " + inputExpression("y", "0.0f") + ", " + inputExpression("z", "0.0f") + ")";
        } else if (type == "texture.componentmask") {
            const auto* channelPin = Input(*node, "channel");
            const auto* channel = channelPin != nullptr ? std::get_if<std::string>(&channelPin->defaultValue) : nullptr;
            const std::string swizzle = (channel != nullptr && (*channel == "g" || *channel == "b")) ? *channel : "r";
            expression = "(" + inputExpression("value", "vec3(0.0f)") + ")." + swizzle;
        } else if (type == "texture.dotproduct") {
            expression = "dot(" + inputExpression("a", "vec3(0.0f)") + ", " + inputExpression("b", "vec3(0.0f)") + ")";
        } else if (type == "texture.crossproduct") {
            expression = "cross(" + inputExpression("a", "vec3(0.0f)") + ", " + inputExpression("b", "vec3(0.0f)") + ")";
        } else if (type == "texture.normalize") {
            expression = "normalize(" + inputExpression("value", "vec3(1.0f, 0.0f, 0.0f)") + ")";
        } else if (type == "texture.fresnel") {
            const std::string exponent = inputExpression("exponent", "5.0f");
            const std::string base = inputExpression("baseReflectFraction", "0.04f");
            expression = "(" + base + " + (1.0f - " + base + ") * pow(clamp(1.0f - dot(worldNormal, cameraVector), 0.0f, 1.0f), " + exponent + "))";
        } else if (type == "texture.panner") {
            expression = "(" + inputExpression("coordinate", "vUV") + " + vec2(" + inputExpression("speedX", "0.0f") + ", "
                       + inputExpression("speedY", "0.0f") + ") * time)";
        } else if (type == "texture.rotator") {
            usesRotator = true;
            expression = "RotateUV(" + inputExpression("coordinate", "vUV") + ", time * " + inputExpression("speed", "0.0f") + " * 6.283185307f)";
        } else if (type == "texture.ddx") {
            expression = "dFdx(" + inputExpression("value", "0.0f") + ")";
        } else if (type == "texture.ddy") {
            expression = "dFdy(" + inputExpression("value", "0.0f") + ")";
        } else if (type == "texture.normalfromheight") {
            usesNormalFromHeight = true;
            expression = "NormalFromHeight(worldNormal, worldPosition, " + inputExpression("height", "0.0f") + ", "
                       + inputExpression("strength", "1.0f") + ")";
        } else if (const auto unary = kUnaryFloatFn.find(type); unary != kUnaryFloatFn.end()) {
            expression = unary->second + "(" + inputExpression("value", "0.0f") + ")";
        } else if (type.starts_with("texture.") && type.ends_with(".float")) {
            const auto op = type.substr(8, type.size() - 14);
            const std::string symbol = op == "add" ? "+" : op == "multiply" ? "*" : op == "subtract" ? "-" : "/";
            expression = "(" + inputExpression("a", "0.0f") + " " + symbol + " " + inputExpression("b", "0.0f") + ")";
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
