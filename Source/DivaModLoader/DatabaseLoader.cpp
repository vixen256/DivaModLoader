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

void loadFilePathCacheSubDirs(std::string modRomDirectory, const char* subDir) {
    WIN32_FIND_DATA fd;
    char buf[MAX_PATH];
    sprintf(buf, "%s\\%s\\*", modRomDirectory.c_str(), subDir);
    HANDLE handle = FindFirstFileA(buf, &fd);
    if (handle == INVALID_HANDLE_VALUE) return;

    do
    {
        if (fd.cFileName[0] == '.')
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            sprintf(buf, "%s/%s", subDir, fd.cFileName);
            loadFilePathCacheSubDirs(modRomDirectory, buf);
        }
        else
        {
            if (*(uint32_t*)fd.cFileName == *(uint32_t*)"mod_")
            {
                prj::string inPath;
                inPath += subDir;
                inPath += "/";
                inPath += MAGIC;
                inPath += modRomDirectory;
                inPath += MAGIC;
                inPath += (fd.cFileName + 3);

                prj::string outPath;
                outPath += modRomDirectory;
                outPath += "/";
                outPath += subDir;
                outPath += "/";
                outPath += fd.cFileName;

                filePathCache.emplace(inPath, outPath);
            }
            else
            {
                prj::string path;
                path += modRomDirectory;
                path += "/";
                path += subDir;
                path += "/";
                path += fd.cFileName;

                prj::string rootPath;
                rootPath += subDir;
                rootPath += "/";
                rootPath += fd.cFileName;

                filePathCache.emplace(path, path);
                filePathCache.emplace(rootPath, path);
            }
        }
    } while (FindNextFileA(handle, &fd));
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
    prj::string filePathCopy;
    if (*(uint16_t*)filePath.c_str() == *(uint16_t*)"./")
        filePathCopy = filePath.substr(2);
    else
        filePathCopy = filePath;

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

    if (destFilePath != nullptr) *destFilePath = out;

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
        loadFilePathCacheSubDirs(*it, "rom");
}