#pragma once

#include <string>
#include <vector>

#include <node_system/graph.h>
#include <node_system/type_registry.h>

namespace creation_texture {

struct TextureSlot {
    std::string path;
    std::string uniformName;
};

struct TextureParameter {
    ce::node_system::DataType type = ce::node_system::DataType::Float;
    ce::node_system::Vec3Default defaultColor = {1.0f, 1.0f, 1.0f};

    std::string name;
    float defaultFloat = 0.0f;
};

struct TextureShaderSource {
    std::string declarations;
    std::string evaluateFunction;
    std::vector<TextureParameter> parameters;
    std::vector<TextureSlot> textures;
};

struct TextureCompileResult {
    bool ok = false;
    TextureShaderSource source;
    std::vector<std::string> errors;
};

TextureCompileResult CompileTextureGraph(const ce::node_system::Graph& graph,
                                         const ce::node_system::NodeTypeRegistry& registry);

} // namespace creation_texture
