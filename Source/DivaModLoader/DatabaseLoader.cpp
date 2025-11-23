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

// both PvLoader and this now have these function ptrs, idk, put them in a header file?
static FUNCTION_PTR(bool, __fastcall, asyncFileLoad, 0x1402A4710, void** fileHandler, const char* file, bool);
static FUNCTION_PTR(bool, __fastcall, asyncFileLoading, 0x151C03830, void** fileHandler);
static FUNCTION_PTR(const void*, __fastcall, asyncFileGetData, 0x151C0EF70, void** fileHandler);
static FUNCTION_PTR(size_t, __fastcall, asyncFileGetSize, 0x151C7ADA0, void** fileHandler);
static FUNCTION_PTR(void, __fastcall, freeAsyncFileHandler, 0x1402A4E90, void** fileHandler);
static FUNCTION_PTR(void, __fastcall, farcParse, 0x1402A0750, void *farc, const void *data, uint64_t size);
static FUNCTION_PTR(void, __fastcall, farcGetFile, 0x1402A1020, void *farc, void *buffer, uint64_t size, int fileIndex);
static FUNCTION_PTR(void, __fastcall, freeFarc, 0x1402A0E00, void *farc);
static FUNCTION_PTR(void, __fastcall, itemTableHandlerParse, 0x1404E8690, void* itemTable, void** fileHandler);

std::vector<void*> itmFileHandlers;
prj::list<prj::string>* mdataPrefixes = nullptr;

HOOK (void, __fastcall, ItemTableHandlerArrayRead, 0x158339FF0)
{
    for (auto it = mdataPrefixes->begin(); it != mdataPrefixes->end(); ++it)
    {
        char buf[MAX_PATH];
        sprintf(buf, "rom/%schritm_prop.farc", it->c_str());
        prj::string path(buf);
        if (implOfResolveFilePath(path, &path))
        {
            void* fileHandler = nullptr;
            asyncFileLoad(&fileHandler, path.c_str(), true);
            itmFileHandlers.push_back(fileHandler);
        }
    }
}

HOOK(bool, __fastcall, ItemTableHandlerArrayLoad, 0x1404E7E60)
{
    for (auto it = itmFileHandlers.begin (); it != itmFileHandlers.end (); ++it)
		if (asyncFileLoading (&*it))
            return true;

    std::vector<void*> farcs;
    for (auto it = itmFileHandlers.begin(); it != itmFileHandlers.end(); ++it)
    {
        void *farc = operatorNew(0x60);
        memset(farc, 0, 0x60);
        farcParse(farc, asyncFileGetData(&*it), asyncFileGetSize(&*it));
        farcs.push_back(farc);

        freeAsyncFileHandler(&*it);
    }
    itmFileHandlers.clear();

    for (int i = 0; i < 10; i++)
    {
        auto ptr = 0x14175B620 + 0x108 * i;
        for (auto it = farcs.begin (); it != farcs.end (); ++it)
        {
            void* buf  = (void*)nullptr;
            uint64_t size = 0;
            auto farcFiles = (prj::vector<void*>*)((uint64_t)*it + 0x38);
            for (int i = 0; i < farcFiles->size(); ++i)
            {
                auto file = farcFiles->at(i);
                if (strcmp((char*)file, *(char**)(ptr + 0x20)) == 0)
                {
                    size = *(int*)((uint64_t)file + 0x88);
                    buf = operatorNew(size);
                    farcGetFile(*it, buf, size, i);
                    break;
                }
            }
            if (buf == nullptr)
                continue;

            void *fakeFileHandlerData[32] = {0};
            fakeFileHandlerData[27] = (void*)size;
            fakeFileHandlerData[28] = buf;
            void* fakeFileHandler = (void*)&fakeFileHandlerData;

            itemTableHandlerParse ((void*)ptr, &fakeFileHandler);

            operatorDelete(buf);
        }
        *(bool*)(ptr + 0x18) = true;
    }

    for (auto it = farcs.begin (); it != farcs.end (); ++it)
    {
        freeFarc(*it);
        operatorDelete(*it);
    }
    farcs.clear();

    return false;
}

void DatabaseLoader::init()
{
    INSTALL_HOOK(ResolveFilePath);

    INSTALL_HOOK(ItemTableHandlerArrayRead);
    INSTALL_HOOK(ItemTableHandlerArrayLoad);
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
    mdataPrefixes = (prj::list<prj::string>*)readInstrPtr(sigInitMdataMgr(), 0xFE, 0x7);

    // Traverse mod folders in reverse to have correct priority.
    for (auto it = modRomDirectoryPaths.rbegin(); it != modRomDirectoryPaths.rend(); ++it)
    {
        prj::string path;

        path += MAGIC;
        path += *it;
        path += MAGIC;
        path += "_";

        mdataPrefixes->push_back(path);
    }

    for (auto it = modRomDirectoryPaths.begin(); it != modRomDirectoryPaths.end(); ++it)
        loadFilePathCacheSubDirs(*it);
}