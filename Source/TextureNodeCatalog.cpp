#include "TextureNodeCatalog.h"

namespace creation_texture::nodes
{

namespace ns = ce::node_system;
using ce::node_system::DataType;
using ce::node_system::Domain;
using ce::node_system::GraphTarget;
using ce::node_system::NodeLibraryDescriptor;
using ce::node_system::NodeLibraryRegistry;
using ce::node_system::NodeTypeDescriptor;
using ce::node_system::PinKind;
using ce::node_system::PinSignature;
using ce::node_system::PinTypeDesc;

namespace
{

PinTypeDesc TexturePin()
{
    return PinTypeDesc{ PinKind::Data, DataType::Texture };
}

PinTypeDesc FloatPin()
{
    return PinTypeDesc{ PinKind::Data, DataType::Float };
}

PinTypeDesc StringPin()
{
    return PinTypeDesc{ PinKind::Data, DataType::String };
}

PinSignature Image(const char* name)
{
    return PinSignature{ name, TexturePin(), {} };
}

// Always an unconnectable "configuration" literal (which imported asset this
// input node reads), same reasoning as Foley's own StringConfig -- not
// something a graph computes and wires in.
PinSignature StringConfig(const char* name, std::string defaultValue = "")
{
    return PinSignature{ name, StringPin(), std::move(defaultValue) };
}

} // namespace

NodeLibraryRegistry BuildTextureNodeCatalog()
{
    NodeLibraryRegistry libraries;

    NodeLibraryDescriptor library;
    library.id = "texture.nodes";
    library.displayName = "Texture Nodes";
    library.description = "Structural boundary nodes for a Djehuti Texture graph -- get an image in, get an image out. Everything in between comes from Frust pods wired in as ordinary nodes.";
    library.target = GraphTarget::Dataflow;

    // Zero inputs besides its own asset-path config, one Texture output --
    // this IS where an imported image enters the graph.
    NodeTypeDescriptor imageInput;
    imageInput.typeName = NodeType::ImageInput;
    imageInput.domain = Domain::Material;
    imageInput.inputs = { StringConfig(PinName::AssetPath) };
    imageInput.outputs = { 
        Image(PinName::Image),
        PinSignature{ PinName::Width, FloatPin(), {} },
        PinSignature{ PinName::Height, FloatPin(), {} }
    };
    imageInput.displayName = "Image Input";
    imageInput.category = "Texture";
    library.nodeTypes.push_back(std::move(imageInput));

    // One Texture input, zero outputs -- the final result the graph renders/
    // exports. A graph with more than one of these is valid (multiple
    // render targets from one graph, e.g. a height map AND a normal map
    // derived from the same source).
    NodeTypeDescriptor imageOutput;
    imageOutput.typeName = NodeType::ImageOutput;
    imageOutput.domain = Domain::Material;
    imageOutput.inputs = { Image(PinName::Image) };
    imageOutput.displayName = "Image Output";
    imageOutput.category = "Texture";
    library.nodeTypes.push_back(std::move(imageOutput));

    NodeTypeDescriptor contrast;
    contrast.typeName = NodeType::ContrastAdjustment;
    contrast.domain = Domain::Material;
    contrast.inputs = { Image(PinName::Image) };
    contrast.outputs = { Image(PinName::Image) };
    contrast.displayName = "Contrast Adjustment";
    contrast.category = "Color";
    library.nodeTypes.push_back(std::move(contrast));

    NodeTypeDescriptor viewer;
    viewer.typeName = NodeType::Viewer;
    viewer.domain = Domain::Material;
    viewer.inputs = { Image(PinName::Image), StringConfig(PinName::ViewerType, "Preview") };
    viewer.displayName = "Viewer";
    viewer.category = "Output";
    library.nodeTypes.push_back(std::move(viewer));

    NodeTypeDescriptor makeTileable;
    makeTileable.typeName = NodeType::MakeTileable;
    makeTileable.domain = Domain::Material;
    makeTileable.inputs = { Image(PinName::Image), { "Edge Blend", FloatPin(), "0.1" } };
    makeTileable.outputs = { Image(PinName::Image) };
    makeTileable.displayName = "Make Tileable";
    makeTileable.category = "Modifiers";
    library.nodeTypes.push_back(std::move(makeTileable));

    NodeTypeDescriptor tileSampler;
    tileSampler.typeName = NodeType::TileSampler;
    tileSampler.domain = Domain::Material;
    tileSampler.inputs = { Image("Pattern"), { "Tiles X", FloatPin(), "8.0" }, { "Tiles Y", FloatPin(), "8.0" }, { "Scale Rand", FloatPin(), "0.5" }, { "Rot Rand", FloatPin(), "1.0" }, { "Pos Rand", FloatPin(), "1.0" } };
    tileSampler.outputs = { Image(PinName::Image) };
    tileSampler.displayName = "Tile Sampler";
    tileSampler.category = "Modifiers";
    library.nodeTypes.push_back(std::move(tileSampler));

    NodeTypeDescriptor dirWarp;
    dirWarp.typeName = NodeType::DirectionalWarp;
    dirWarp.domain = Domain::Material;
    dirWarp.inputs = { Image(PinName::Image), Image("Intensity Map"), { "Angle", FloatPin(), "0.0" }, { "Intensity", FloatPin(), "10.0" } };
    dirWarp.outputs = { Image(PinName::Image) };
    dirWarp.displayName = "Directional Warp";
    dirWarp.category = "Modifiers";
    library.nodeTypes.push_back(std::move(dirWarp));

    // --- Math ---
    for (const auto& [typeSuffix, display, symbol] : {
             std::tuple{"add", "Add", "+"}, std::tuple{"subtract", "Subtract", "-"},
             std::tuple{"multiply", "Multiply", "*"}, std::tuple{"divide", "Divide", "/"}}) {
        library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = std::string("texture.") + typeSuffix + ".float", .domain = ns::Domain::Material,
                           .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                      {"b", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                           .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                           .displayName = display, .category = "Math"});
    }
    for (const auto& [typeSuffix, display] : {
             std::pair{"abs", "Abs"}, std::pair{"ceil", "Ceil"}, std::pair{"floor", "Floor"},
             std::pair{"frac", "Frac"}, std::pair{"sqrt", "SquareRoot"}, std::pair{"sine", "Sine"},
             std::pair{"cosine", "Cosine"}, std::pair{"oneminus", "OneMinus"}}) {
        library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = std::string("texture.") + typeSuffix, .domain = ns::Domain::Material,
                           .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                           .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                           .displayName = display, .category = "Math"});
    }
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.power", .domain = ns::Domain::Material,
                       .inputs = {{"base", {ns::PinKind::Data, ns::DataType::Float}, 1.0f},
                                  {"exponent", {ns::PinKind::Data, ns::DataType::Float}, 1.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "Power", .category = "Math"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.fmod", .domain = ns::Domain::Material,
                       .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"b", {ns::PinKind::Data, ns::DataType::Float}, 1.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "Fmod", .category = "Math"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.clamp", .domain = ns::Domain::Material,
                       .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"min", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"max", {ns::PinKind::Data, ns::DataType::Float}, 1.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "Clamp", .category = "Math"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.lerp", .domain = ns::Domain::Material,
                       .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"b", {ns::PinKind::Data, ns::DataType::Float}, 1.0f},
                                  {"alpha", {ns::PinKind::Data, ns::DataType::Float}, 0.5f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "LinearInterpolate", .category = "Math"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.if", .domain = ns::Domain::Material,
                       .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"b", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"aGreaterThanB", {ns::PinKind::Data, ns::DataType::Float}, 1.0f},
                                  {"aEqualsB", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"aLessThanB", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "If", .category = "Math"});

    // --- Vector Ops ---
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.append", .domain = ns::Domain::Material,
                       .inputs = {{"x", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"y", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"z", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, {}}},
                       .displayName = "AppendVector", .category = "Vector Ops"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.componentmask", .domain = ns::Domain::Material,
                       .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{}},
                                  {"channel", {ns::PinKind::Data, ns::DataType::String}, std::string("r")}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "ComponentMask", .category = "Vector Ops",
                       .description = "channel is \"r\", \"g\", or \"b\"."});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.dotproduct", .domain = ns::Domain::Material,
                       .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{}},
                                  {"b", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{}}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "DotProduct", .category = "Vector Ops"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.crossproduct", .domain = ns::Domain::Material,
                       .inputs = {{"a", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{}},
                                  {"b", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{}}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, {}}},
                       .displayName = "CrossProduct", .category = "Vector Ops"});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.normalize", .domain = ns::Domain::Material,
                       .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, ns::Vec3Default{1.0f, 0.0f, 0.0f}}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, {}}},
                       .displayName = "Normalize", .category = "Vector Ops"});

    // --- Utility ---
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.fresnel", .domain = ns::Domain::Material,
                       .inputs = {{"exponent", {ns::PinKind::Data, ns::DataType::Float}, 5.0f},
                                  {"baseReflectFraction", {ns::PinKind::Data, ns::DataType::Float}, 0.04f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "Fresnel", .category = "Utility",
                       .description = "View-angle-dependent edge term, using the surface's own normal and camera vector."});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.ddx", .domain = ns::Domain::Material,
                       .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "DDX", .category = "Utility",
                       .description = "Screen-space partial derivative in the X direction. Fragment-stage "
                                      "only (baseColor/metallic/roughness/normal) -- dFdx has no vertex-shader "
                                      "equivalent, so wiring this into World Position Offset won't compile."});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.ddy", .domain = ns::Domain::Material,
                       .inputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, 0.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Float}, {}}},
                       .displayName = "DDY", .category = "Utility",
                       .description = "Screen-space partial derivative in the Y direction. Fragment-stage "
                                      "only, same reason as DDX."});
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.normalfromheight", .domain = ns::Domain::Material,
                       .inputs = {{"height", {ns::PinKind::Data, ns::DataType::Float}, 0.0f},
                                  {"strength", {ns::PinKind::Data, ns::DataType::Float}, 1.0f}},
                       .outputs = {{"value", {ns::PinKind::Data, ns::DataType::Color}, {}}},
                       .displayName = "Normal From Height", .category = "Utility",
                       .description = "Live surface-gradient bump mapping (Blinn 1978) from a height/luminance "
                                      "signal -- e.g. a Texture Sample's red channel via ComponentMask. No "
                                      "baking, no tangent basis needed: perturbs the surface's own world normal "
                                      "using screen-space derivatives of height and world position. "
                                      "Fragment-stage only (uses DDX/DDY internally) -- valid on Material "
                                      "Output's Normal input, not on World Position Offset."});

    // --- Texture ---
    library.nodeTypes.push_back(NodeTypeDescriptor{.typeName = "texture.texture.sample2d", .domain = ns::Domain::Material,
                       .inputs = {{"texture", {ns::PinKind::Data, ns::DataType::Texture}, std::string("")},
                                  {"uv", {ns::PinKind::Data, ns::DataType::Vec2}, {}}},
                       .outputs = {{"color", {ns::PinKind::Data, ns::DataType::Color}, {}}},
                       .displayName = "Texture Sample", .category = "Texture",
                       .description = "texture is an absolute file path (e.g. C:/art/rock.png), resolved into a "
                                      "real GPU texture when the graph compiles -- no asset picker or "
                                      "drag-and-drop yet, type the path directly."});

    
    libraries.Register(std::move(library));
    return libraries;
}

} // namespace creation_texture::nodes
