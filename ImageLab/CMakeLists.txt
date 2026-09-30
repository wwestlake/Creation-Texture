# Image Lab core: layers, colour conversion, and the FRust pixel routines - no UI.
# The FRust sources are embedded in the library and compiled on first use; nothing is loaded from disk.

juce_add_binary_data(creation_texture_image_lab_frust
    HEADER_NAME ImageLabFrustSources.h
    NAMESPACE ImageLabFrust
    SOURCES
        Frust/image_lab_core.frust
        Frust/surface_maps.frust
        Frust/image_graph.frust
        Frust/image_fx.frust
        Frust/pods/frust_image_demo/lib.fr
)

add_library(creation_texture_image_lab STATIC
    ImageLabCore.cpp
    ImageLabCore.h
    SurfaceMaps.cpp
    SurfaceMaps.h
    TextureSet.cpp
    TextureSet.h
    ImageGraph.cpp
    ImageGraph.h
    ImageDemoHost.cpp
    ImageDemoHost.h
)

target_include_directories(creation_texture_image_lab PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(creation_texture_image_lab
    PUBLIC
        juce::juce_graphics
        creation_suite_node_system
    PRIVATE
        creation_texture_image_lab_frust
        creation_suite_frust_plugin_runtime
)
target_compile_features(creation_texture_image_lab PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(creation_texture_image_lab PRIVATE /FS /MP1)
endif()

add_executable(CreationTextureImageLabSmoke "${CMAKE_CURRENT_SOURCE_DIR}/../tests/ImageLabSmoke.cpp")
target_link_libraries(CreationTextureImageLabSmoke PRIVATE creation_texture_image_lab)

set_target_properties(creation_texture_image_lab PROPERTIES OUTPUT_NAME "djehuti_texture_image_lab")
set_target_properties(CreationTextureImageLabSmoke PROPERTIES OUTPUT_NAME "DjehutiTextureImageLabSmoke")

add_executable(CreationTextureSurfaceMapsSmoke "${CMAKE_CURRENT_SOURCE_DIR}/../tests/SurfaceMapsSmoke.cpp")
target_link_libraries(CreationTextureSurfaceMapsSmoke PRIVATE creation_texture_image_lab)
set_target_properties(CreationTextureSurfaceMapsSmoke PROPERTIES OUTPUT_NAME "DjehutiTextureSurfaceMapsSmoke")

# Developer tool: compile one FRust file in-process, with crash dialogs off.
add_executable(CreationTextureFrustProbe "${CMAKE_CURRENT_SOURCE_DIR}/../tests/FrustProbe.cpp")
target_link_libraries(CreationTextureFrustProbe PRIVATE creation_suite_frust_plugin_runtime)
set_target_properties(CreationTextureFrustProbe PROPERTIES OUTPUT_NAME "DjehutiTextureFrustProbe")

# Developer tool: times the Surface Map save pipeline stage by stage.
add_executable(CreationTextureSurfaceMapsBench "${CMAKE_CURRENT_SOURCE_DIR}/../tests/SurfaceMapsBench.cpp")
target_link_libraries(CreationTextureSurfaceMapsBench PRIVATE creation_texture_image_lab)
set_target_properties(CreationTextureSurfaceMapsBench PROPERTIES OUTPUT_NAME "DjehutiTextureSurfaceMapsBench")

add_executable(CreationTextureImageGraphSmoke "${CMAKE_CURRENT_SOURCE_DIR}/../tests/ImageGraphSmoke.cpp")
target_link_libraries(CreationTextureImageGraphSmoke PRIVATE creation_texture_image_lab)
set_target_properties(CreationTextureImageGraphSmoke PROPERTIES OUTPUT_NAME "DjehutiTextureImageGraphSmoke")
