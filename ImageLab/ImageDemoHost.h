#pragma once

#include <creation/frust/PluginRuntime.h>

// Host functions the frust_image_demo pod calls (see ImageDemoHost.cpp).
namespace image_demo_host
{
void registerAll(creation::frust::PluginRuntime& runtime);

// Frees the arrays the pod allocated during the last generator run on this thread.
void freeArena();
}
