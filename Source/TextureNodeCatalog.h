#pragma once

#include "node_system/node_library.h"

// Djehuti Texture's own node catalog -- deliberately separate from every
// other domain's (Foley's, Signal Lab's, ...), per the suite's explicit
// consolidation stance: share the generic node-graph editing machinery
// (ce::node_system::Graph/NodeLibraryRegistry, and the ported
// NodeGraphComponent/NodeInspector/NodePalette UI in shared/NodeEditorUI)
// across every domain, but never merge domain-specific node catalogs.
//
// Texture is a plain dataflow graph (GraphTarget::Dataflow, not an exec
// chain like Foley's Behavior graphs) -- an image goes in, a chain of
// transforms runs over it, a result comes out. There is deliberately no
// OnTrigger/exec-pin shape here.
//
// This catalog only defines the two structural boundary nodes every Texture
// graph needs (Image Input, Image Output) -- everything a graph actually
// DOES to the image in between is meant to come from Frust pods (the suite's
// growing math/procedural pod registry) wired in as ordinary nodes, not
// hand-written C++ node types added here one at a time. No automatic
// pod-to-node mechanism exists yet anywhere in the suite; that is a real,
// separate, deliberately-deferred piece of work, not part of this catalog.

namespace ce::node_system
{
class NodeLibraryRegistry;
}

namespace creation_texture::nodes
{

namespace NodeType
{
inline constexpr const char* ImageInput = "texture.imageInput";
inline constexpr const char* ImageOutput = "texture.imageOutput";
} // namespace NodeType

namespace PinName
{
inline constexpr const char* Image = "image";
inline constexpr const char* AssetPath = "assetPath";
} // namespace PinName

// Builds a fresh registry containing Texture's structural nodes. Returned by
// value, same reasoning as Foley/Signal Lab's own BuildXNodeCatalog --
// registrations are cheap, callers shouldn't have to share one global
// instance.
ce::node_system::NodeLibraryRegistry BuildTextureNodeCatalog();

} // namespace creation_texture::nodes
