add_library(creation_texture_language_host STATIC
    AppLanguagePolicy.cpp
    AppLanguagePolicy.h
    TexturePluginHost.cpp
    TexturePluginHost.h
)

target_include_directories(creation_texture_language_host PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(creation_texture_language_host PUBLIC juce::juce_graphics)

if(MSVC)
    target_compile_options(creation_texture_language_host PRIVATE /FS /MP1)
endif()

if(CREATION_TEXTURE_ENABLE_SCRIPTING)
    target_include_directories(creation_texture_language_host PRIVATE "${CREATION_TEXTURE_LLVM_ROOT}/include")
    target_compile_definitions(creation_texture_language_host PUBLIC CREATION_TEXTURE_ENABLE_SCRIPTING=1)
else()
    target_compile_definitions(creation_texture_language_host PUBLIC CREATION_TEXTURE_ENABLE_SCRIPTING=0)
endif()

if(TARGET creation_suite_frust_plugin_runtime)
    target_link_libraries(creation_texture_language_host PUBLIC creation_suite_frust_plugin_runtime)
    target_compile_definitions(creation_texture_language_host PUBLIC CREATION_TEXTURE_ENABLE_FRUST_PLUGINS=1)
else()
    target_compile_definitions(creation_texture_language_host PUBLIC CREATION_TEXTURE_ENABLE_FRUST_PLUGINS=0)
endif()


# Auto-generated output name overrides
set_target_properties(creation_texture_language_host PROPERTIES OUTPUT_NAME "djehuti_texture_language_host")
