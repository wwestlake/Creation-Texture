cmake_minimum_required(VERSION 3.22)

set(CreationTexture_VERSION "0.0.1" CACHE STRING "Djehuti Texture product version")
project(CreationTexture VERSION ${CreationTexture_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../shared/CMake/CreationSuiteBuildSettings.cmake")
creation_suite_require_juce()

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

set(CREATION_SUITE_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared" CACHE PATH "Path to the Djehuti Suite shared code root")

include(cmake/CreationSharedLLVM.cmake)
configure_creation_shared_llvm(CREATION_TEXTURE "Djehuti Texture")

add_subdirectory("${JUCE_DIR}" juce)

set(CREATION_TEXTURE_ASSET_SYSTEM_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/AssetSystem/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/AssetSystem"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedAssetSystem")
    set(CREATION_TEXTURE_ASSET_SYSTEM_TARGET creation_suite_asset_system)
endif()

set(CREATION_TEXTURE_VFS_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/VFS/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/VFS"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedVFS")
    set(CREATION_TEXTURE_VFS_TARGET creation_suite_vfs)
endif()

set(CREATION_TEXTURE_SERVICES_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Services/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Services"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedServices")
    set(CREATION_TEXTURE_SERVICES_TARGET creation_suite_services)
endif()

set(CREATION_TEXTURE_UI_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/UI/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/UI"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedUI")
    set(CREATION_TEXTURE_UI_TARGET creation_suite_ui)
endif()

set(CREATION_TEXTURE_DOCK_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/JuceDocking/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/JuceDocking"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedJuceDocking")
    set(CREATION_TEXTURE_DOCK_TARGET CreationDock)
endif()

set(CREATION_TEXTURE_RENDER_TARGET "")
if(EXISTS "/Render/CMakeLists.txt")
    add_subdirectory("/Render"
                     "/CreationSuiteSharedRender")
    set(CREATION_TEXTURE_RENDER_TARGET creation_suite_render)
endif()

set(CREATION_TEXTURE_INTEROP_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Interop/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Interop"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedInterop")
    set(CREATION_TEXTURE_INTEROP_TARGET creation_suite_interop)
endif()


set(CREATION_TEXTURE_MATERIAL_SYSTEM_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/MaterialSystem/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/MaterialSystem" "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedMaterialSystem")
    set(CREATION_TEXTURE_MATERIAL_SYSTEM_TARGET creation_suite_material_system)
endif()

if(EXISTS "${CREATION_SUITE_SHARED_DIR}/FrustPluginRuntime/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/FrustPluginRuntime"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedFrustPluginRuntime")
endif()

# The node-graph substrate is suite-owned (docs/SHARED_EXTRACTION_PLAN.md),
# shared with Engine and Station rather than forked per-app -- see
# CreationEngine/CMakeLists.txt's identical comment. node_system carries the
# graph model, pin/type registry, and graph->FRust codegen; NodeEditorUI is
# just the canvas widget on top of it. Both already existed for other apps;
# Texture simply never linked them.
set(CREATION_TEXTURE_NODE_EDITOR_UI_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/NodeEditorUI/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/NodeEditorUI"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedNodeEditorUI")
    set(CREATION_TEXTURE_NODE_EDITOR_UI_TARGET creation_suite_node_editor_ui)
endif()
if(NOT TARGET node_system)
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/NodeSystem" CreationSuiteSharedNodeSystem)
endif()

add_subdirectory(Language)

juce_add_gui_app(CreationTextureApp
    PRODUCT_NAME "DjehutiTexture"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationtexture"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI/assets/creation_texture.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/../../shared/UI/assets/creation_texture.png"
)

creation_suite_add_shared_bin_copy(CreationTextureApp)
creation_suite_get_shared_bin_dir(CREATION_TEXTURE_SHARED_BIN_DIR)

add_custom_command(TARGET CreationTextureApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CreationTextureApp>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/TextureAdjustments.frust" "$<TARGET_FILE_DIR:CreationTextureApp>/plugins/"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CREATION_TEXTURE_SHARED_BIN_DIR}/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/TextureAdjustments.frust" "${CREATION_TEXTURE_SHARED_BIN_DIR}/plugins/"
    COMMENT "Copying built-in FRust texture plugins"
)

juce_generate_juce_header(CreationTextureApp)

target_sources(CreationTextureApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
    Source/NodeGraphPanel.cpp
    Source/ViewerPanel.cpp
    Source/NodeGraphPanel.h
    Source/ViewerPanel.h
    Source/ViewerNodeEditor.cpp
    Source/ViewerNodeEditor.h
    Source/ContrastAdjustmentEditor.cpp
    Source/ContrastAdjustmentEditor.h
    Source/Branding.cpp
    Source/Branding.h
    Source/Shared/CreationSuiteHeaderBar.h
)

target_compile_definitions(CreationTextureApp PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:CreationTextureApp,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:CreationTextureApp,JUCE_VERSION>"
)

target_link_libraries(CreationTextureApp PRIVATE
    ${CREATION_TEXTURE_ASSET_SYSTEM_TARGET}
    ${CREATION_TEXTURE_VFS_TARGET}
    ${CREATION_TEXTURE_SERVICES_TARGET}
    ${CREATION_TEXTURE_UI_TARGET}
    ${CREATION_TEXTURE_DOCK_TARGET}
    ${CREATION_TEXTURE_RENDER_TARGET}
    ${CREATION_TEXTURE_INTEROP_TARGET}
    ${CREATION_TEXTURE_NODE_EDITOR_UI_TARGET}
    creation_suite_node_system
    ${CREATION_TEXTURE_MATERIAL_SYSTEM_TARGET}
    creation_texture_language_host
    juce::juce_gui_extra
    juce::juce_opengl
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

if(MSVC)
    target_compile_options(CreationTextureApp PRIVATE /FS /MP1)
endif()

install(TARGETS CreationTextureApp RUNTIME DESTINATION . COMPONENT CreationTexture)
install(FILES BuiltInPlugins/TextureAdjustments.frust BuiltInPlugins/TextureCompositor.frust DESTINATION plugins COMPONENT CreationTexture)

add_executable(CreationTexturePluginSmoke tests/TexturePluginSmoke.cpp)
target_link_libraries(CreationTexturePluginSmoke PRIVATE creation_suite_frust_plugin_runtime)
target_compile_definitions(CreationTexturePluginSmoke PRIVATE
    CREATION_TEXTURE_ADJUSTMENTS_PLUGIN="${CMAKE_CURRENT_SOURCE_DIR}/BuiltInPlugins/TextureAdjustments.frust"
)


add_custom_command(TARGET CreationTextureApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/gemini-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationTextureApp>" "D:/CreationSuite-Workspaces/gemini-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationTextureApp to the shared gemini bin directory"
)

add_custom_command(TARGET CreationTextureApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationTextureApp>" "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationTextureApp to the shared claude bin directory"
)

# Auto-generated output name overrides
set_target_properties(CreationTexturePluginSmoke PROPERTIES OUTPUT_NAME "DjehutiTexturePluginSmoke")
set_target_properties(CreationTextureApp PROPERTIES OUTPUT_NAME "DjehutiTextureApp")
