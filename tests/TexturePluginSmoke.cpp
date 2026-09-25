#include <creation/frust/PluginRuntime.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{
extern "C" double suite_texture_pow(double value, double exponent)
{
    return std::pow(value, exponent);
}

using ApplyAdjustmentsFunction = int64_t (*)(uint8_t*, int64_t, int64_t, int64_t, double, double, double, double);
}

int main()
{
    creation::frust::PluginRuntime runtime("creation-texture");
    runtime.registerHostFunction("suite_texture_pow", reinterpret_cast<void*>(&suite_texture_pow));

    std::string error;
    if (! runtime.load(CREATION_TEXTURE_ADJUSTMENTS_PLUGIN, error))
    {
        std::cerr << "Could not load Texture Adjustments plugin: " << error << '\n';
        return 1;
    }

    auto apply = reinterpret_cast<ApplyAdjustmentsFunction>(runtime.getFunction("texture_apply_adjustments"));
    if (apply == nullptr)
    {
        std::cerr << "Texture Adjustments plugin has no texture_apply_adjustments entry point.\n";
        return 1;
    }

    uint8_t rgba[] { 128, 64, 255, 200 };
    if (apply(rgba, 1, 1, 4, 0.0, 1.0, 1.0, 1.0) != 1)
    {
        std::cerr << "Texture Adjustments plugin rejected a valid RGBA buffer.\n";
        return 1;
    }

    if (rgba[3] != 200)
    {
        std::cerr << "Texture Adjustments plugin changed alpha unexpectedly.\n";
        return 1;
    }

    std::cout << "Texture Adjustments FRust plugin loaded and processed RGBA data.\n";
    return 0;
}
