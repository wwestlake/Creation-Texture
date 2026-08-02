#include "AppLanguagePolicy.h"

#if CREATION_TEXTURE_ENABLE_SCRIPTING
 #include <llvm/Config/llvm-config.h>
#endif

namespace creation_texture::language
{
std::string getLanguageRuntimeSummary()
{
#if CREATION_TEXTURE_ENABLE_SCRIPTING
    return "LLVM-enabled host layer ready for suite domain scripting (" LLVM_VERSION_STRING ")";
#else
    return "LLVM scripting disabled for this build.";
#endif
}

std::string getAppDomainName()
{
    return "texture";
}

bool canRunNodeDomain(const std::string& domainName)
{
    return domainName == "shared" || domainName == "texture";
}
}

