cmake_minimum_required(VERSION 3.22)

set(CreationTexture_VERSION "0.0.1" CACHE STRING "Creation Texture product version")
project(CreationTexture VERSION ${CreationTexture_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " /Zi")
    string(APPEND CMAKE_EXE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_RELEASE " /DEBUG /OPT:REF /OPT:ICF")
endif()

if(NOT DEFINED JUCE_DIR)
    message(FATAL_ERROR "Set JUCE_DIR to your JUCE root directory before configuring.")
endif()

set(CREATION_SUITE_SHARED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../shared" CACHE PATH "Path to the Creation Suite shared code root")

include(cmake/CreationSharedLLVM.cmake)
configure_creation_shared_llvm(CREATION_TEXTURE "Creation Texture")

add_subdirectory("${JUCE_DIR}" juce)
add_subdirectory(Language)

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

set(CREATION_TEXTURE_INTEROP_TARGET "")
if(EXISTS "${CREATION_SUITE_SHARED_DIR}/Interop/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/Interop"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedInterop")
    set(CREATION_TEXTURE_INTEROP_TARGET creation_suite_interop)
endif()

if(EXISTS "${CREATION_SUITE_SHARED_DIR}/JuceDocking/CMakeLists.txt")
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/JuceDocking"
                     "${CMAKE_CURRENT_BINARY_DIR}/CreationSuiteSharedDocking")
endif()

# Texture's CEL surface -- suite-wide language, not a bespoke per-app one.
# ce_lang_frontend/node_system build with no LLVM dependency; only the
# JIT half of shared/CEL needs LLVM, and Texture doesn't link that (yet).
if(NOT TARGET node_system)
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/NodeSystem" CreationSuiteSharedNodeSystem)
endif()
if(NOT TARGET ce_lang_frontend)
    add_subdirectory("${CREATION_SUITE_SHARED_DIR}/CEL" CreationSuiteSharedCel)
endif()

juce_add_gui_app(CreationTextureApp
    PRODUCT_NAME "Creation Texture"
    COMPANY_NAME "LagDaemon Software"
    BUNDLE_ID "com.lagdaemon.creationtexture"
    ICON_BIG "${CREATION_SUITE_SHARED_DIR}/UI/assets/creation_texture.png"
    ICON_SMALL "${CREATION_SUITE_SHARED_DIR}/UI/assets/creation_texture.png"
)

# Mirrors the built executable into the workspace-level shared bin
# directory (D:\CreationSuite-Workspaces\claude-<config>-bin) so it's
# reachable at a stable path without needing OS symlink privileges.
# Every suite agent does this for its own repo -- see AGENTS.md.
add_custom_command(TARGET CreationTextureApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:CreationTextureApp>" "D:/CreationSuite-Workspaces/claude-$<LOWER_CASE:$<CONFIG>>-bin/"
    COMMENT "Copying CreationTextureApp to the shared claude bin directory"
)

juce_generate_juce_header(CreationTextureApp)

target_include_directories(CreationTextureApp PRIVATE
    "${CREATION_SUITE_SHARED_DIR}/JuceDocking/include"
)

target_sources(CreationTextureApp PRIVATE
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/MainComponent.h
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
    ${CREATION_TEXTURE_SERVICES_TARGET}
    ${CREATION_TEXTURE_UI_TARGET}
    ${CREATION_TEXTURE_INTEROP_TARGET}
    creation_texture_language_host
    ce_lang_frontend
    node_system
    CreationDock
    juce::juce_gui_extra
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

install(TARGETS CreationTextureApp
    RUNTIME DESTINATION .
    RESOURCE DESTINATION .
)
install(FILES README.md LICENSE DESTINATION .)

set(CPACK_PACKAGE_NAME "Creation Texture")
set(CPACK_PACKAGE_VENDOR "LagDaemon Software")
set(CPACK_PACKAGE_CONTACT "LagDaemon Software")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Creation Texture")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "creation-texture-${CPACK_PACKAGE_VERSION}-win64")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Creation Texture")
set(CPACK_GENERATOR "ZIP;WIX")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Creation Texture")
set(CPACK_PACKAGE_EXECUTABLES "Creation Texture" "Creation Texture")
set(CPACK_CREATE_DESKTOP_LINKS "Creation Texture")

include(CPack)

