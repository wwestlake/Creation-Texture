#include "TextureNodeCatalog.h"

namespace creation_texture::nodes
{

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
    imageInput.outputs = { Image(PinName::Image) };
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

    libraries.Register(std::move(library));
    return libraries;
}

} // namespace creation_texture::nodes
