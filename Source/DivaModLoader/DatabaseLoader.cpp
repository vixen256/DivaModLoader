#include "DatabaseLoader.h"

#include "Context.h"
#include "ModLoader.h"
#include "SigScan.h"
#include "Types.h"
#include "Utilities.h"

// The game contains a list of database prefixes in the mount data manager.
// We insert all mod directory paths into this list along with a magic value wrapping it.

// For example, object database becomes "rom/objset/<magic><mod path><magic>_obj_db.bin".
// We detect this pattern in the file resolver function and fix it to become a valid file path.
// It becomes "<mod path>/rom/objset/mod_obj_db.bin" as a result.

constexpr char MAGIC = 0x01;

std::unordered_map<prj::string, std::optional<prj::string>> filePathCache;

void loadFilePathCacheSubDirs(std::string modRomDirectory) {
    for (const auto& file : std::filesystem::recursive_directory_iterator(modRomDirectory))
    {
        if (!file.is_regular_file())
            continue;

        auto dir = file.path().lexically_relative(modRomDirectory).remove_filename().string();
        auto filename = file.path().filename().string();
        std::replace(dir.begin(), dir.end(), '\\', '/');

        if (*(uint32_t*)filename.c_str() == *(uint32_t*)"mod_")
        {
            prj::string inPath;
            inPath += dir;
            inPath += MAGIC;
            inPath += modRomDirectory;
            inPath += MAGIC;
            inPath += (filename.c_str() + 3);
            std::transform(inPath.begin(), inPath.end(), inPath.begin(), tolower);

            prj::string outPath;
            outPath += modRomDirectory;
            outPath += '/';
            outPath += dir;
            outPath += filename;

            filePathCache.emplace(inPath, outPath);
        }
        else
        {
            prj::string path;
            path += modRomDirectory;
            path += '/';
            path += dir;
            path += filename;
            std::transform(path.begin(), path.end(), path.begin(), tolower);

            prj::string rootPath;
            rootPath += dir;
            rootPath += filename;
            std::transform(rootPath.begin(), rootPath.end(), rootPath.begin(), tolower);

            filePathCache.emplace(path, path);
            filePathCache.emplace(rootPath, path);
        }
    }
}

SIG_SCAN
(
    sigResolveFilePath,
    0x14026745B,
    "\xE8\xCC\xCC\xCC\xCC\x4C\x8B\x65\xF0", 
    "x????xxxx"
); // call to function, E8 ?? ?? ?? ??

HOOK(size_t, __fastcall, ResolveFilePath, readInstrPtr(sigResolveFilePath(), 0, 0x5), prj::string& filePath, prj::string* destFilePath)
{
    prj::string filePathCopy = std::filesystem::path(filePath).lexically_normal().string().c_str();
    std::replace(filePathCopy.begin(), filePathCopy.end(), '\\', '/');
    std::transform(filePathCopy.begin(), filePathCopy.end(), filePathCopy.begin(), tolower);

    auto cachedResult = filePathCache.find(filePathCopy);
    if (cachedResult != filePathCache.end())
    {
        bool result = cachedResult->second.has_value();
        if (result && destFilePath != nullptr)
            *destFilePath = cachedResult->second.value();
        return result;
    }

    // Because we cache all mod files, any path with MAGIC cannot exist outside of cache and we can simply skip
    if (filePath.find(MAGIC) != std::string::npos)
        return false;

    prj::string out;
    bool result = originalResolveFilePath(filePath, &out);

    if (destFilePath != nullptr)
        *destFilePath = out;

    if (result)
        filePathCache.emplace(filePathCopy, out);
    else
        filePathCache.emplace(filePathCopy, std::nullopt);

    return result;
}

void DatabaseLoader::init()
{
    INSTALL_HOOK(ResolveFilePath);
}

SIG_SCAN
(
    sigInitMdataMgr,
    0x14043E050,
    "\x48\x89\x5C\x24\x08\x48\x89\x6C\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7C\x24\x20\x41\x54\x41\x56\x41\x57\x48\x83\xEC\x60\x48\x8B\x44",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
);

void DatabaseLoader::initMdataMgr(const std::vector<std::string>& modRomDirectoryPaths)
{
    // Get the list address from the lea instruction that loads it.
    auto& list = *(prj::list<prj::string>*)readInstrPtr(sigInitMdataMgr(), 0xFE, 0x7);

    // Traverse mod folders in reverse to have correct priority.
    for (auto it = modRomDirectoryPaths.rbegin(); it != modRomDirectoryPaths.rend(); ++it)
    {
        prj::string path;

        path += MAGIC;
        path += *it;
        path += MAGIC;
        path += "_";

        list.push_back(path);
    }

    for (auto it = modRomDirectoryPaths.begin(); it != modRomDirectoryPaths.end(); it++)
        loadFilePathCacheSubDirs(*it);
}