#pragma once
#include "Types.h"

class DatabaseLoader
{
public:
    static void init();
    static void initMdataMgr(const std::vector<std::string>& modRomDirectoryPaths, prj::vector<prj::string>* romDirectoryPaths);
};
