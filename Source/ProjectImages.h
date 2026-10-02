#pragma once

#include <JuceHeader.h>
#include <creation/assets/ProjectSession.h>
#include "ProjectImagePicker.h"

#include <map>
#include <string>

// The open project's images, read from the project and kept: the list of them, full images, and small thumbnails.
// Every tool that takes an image in context (an image slot, a picker) gets them from here.
class ProjectImages final
{
public:
    void setProjectSession(creation::assets::ProjectSession* session);
    // Forget what was read (another project was opened, or the project's images changed).
    void clear();

    // The project's images, for an image slot and its picker.
    project_images::Source source();

    juce::Image image(const juce::String& logicalPath);
    juce::Image thumbnail(const juce::String& logicalPath);

    // Whether a project asset is an image this app can read.
    static bool isImageAsset(const creation::assets::AssetDescriptor& asset);

private:
    creation::assets::ProjectSession* projectSession = nullptr;
    std::map<std::string, juce::Image> images;
    std::map<std::string, juce::Image> thumbnails;
};
