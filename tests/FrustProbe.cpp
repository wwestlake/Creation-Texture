// Compiles one FRust file through the same in-process runtime the app uses, and reports whether it loaded.
// A developer tool for narrowing down FRust compiler problems without crash dialogs.
//   DjehutiTextureFrustProbe.exe <file.frust> [function-name]

#include "NoCrashDialogs.h"

#include <creation/frust/PluginRuntime.h>

#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    disableCrashDialogs();
    if (argc < 2)
    {
        std::cerr << "usage: DjehutiTextureFrustProbe <file.frust> [function-name]\n";
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (! in)
    {
        std::cerr << "cannot open " << argv[1] << "\n";
        return 2;
    }
    std::stringstream text;
    text << in.rdbuf();

    creation::frust::PluginRuntime runtime("creation-texture");
    ::frust::CompileRequest request;
    request.sources.push_back({ argv[1], text.str() });

    std::string error;
    if (! runtime.loadSource("probe", request, error))
    {
        std::cout << "COMPILE ERROR: " << error << "\n";
        return 1;
    }
    if (argc > 2 && runtime.getFunction("probe", argv[2]) == nullptr)
    {
        std::cout << "LOADED, but no function " << argv[2] << "\n";
        return 1;
    }
    std::cout << "LOADED\n";
    return 0;
}
