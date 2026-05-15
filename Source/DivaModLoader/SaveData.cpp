#include "SaveData.h"

#include "Types.h"
#include "SigScan.h"

struct Score
{
    int32_t pvId;
    INSERT_PADDING(0x132C);
};

struct Module
{
    uint8_t unknown0;
    uint8_t unknown1;
};

struct ModuleEx // used by DML save, game's own save data doesn't store module ID
{
    uint32_t moduleId;
    Module module;
};

struct CstmItem 
{
    uint8_t unknown0;
};

struct CstmItemEx  // used by DML save, game's own save data doesn't store customize item ID
{
    uint32_t cstmItemId;
    CstmItem cstmItem;
};

static std::unordered_map<uint32_t, Score> scoreMap;
static std::unordered_map<uint32_t, Module> moduleMap;
static std::unordered_map<uint32_t, CstmItem> cstmItemMap;

struct SaveDataEx
{
    static constexpr uint32_t MAX_VERSION = 1; // DO NOT INCREASE THIS ANYMORE!!! Refer to LoadSaveData for the reason why.
    static constexpr char FILE_NAME[] = "DivaModLoader.dat";

    uint32_t version;
    uint32_t headerSize;

    uint32_t scoreCount;
    uint32_t moduleCount;
    uint32_t cstmItemCount;
    // ...add more data in new versions as necessary

    Score* getScores()
    {
        return reinterpret_cast<Score*>(reinterpret_cast<uint8_t*>(this) + headerSize);
    }

    ModuleEx* getModules()
    {
        return reinterpret_cast<ModuleEx*>(getScores() + scoreCount); // placed right after scores
    }

    CstmItemEx* getCstmItems()
    {
        return reinterpret_cast<CstmItemEx*>(getModules() + moduleCount); // placed right after modules
    }

    // ...add more functions in new versions as necessary
};

SIG_SCAN
(
    sigGetSaveDataFilePath,
    0x1401D70D0,
    "\x48\x8B\xC4\x48\x89\x58\x18\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\xA8\xE8\xFB\xFF\xFF\x48\x81\xEC\xE0\x04\x00\x00\x0F\x29\x70\xB8\x0F\x29\x78\xA8\x48", 
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
);

SIG_SCAN
(
    sigGetSaveDataKey,
    0x1401D76F0,
    "\x48\x89\x5C\x24\x18\x48\x89\x74\x24\x20\x55\x57\x41\x54\x41\x56\x41\x57\x48\x8B\xEC\x48\x83\xEC\x60", 
    "xxxxxxxxxxxxxxxxxxxxxxxxx"
);

static FUNCTION_PTR(void, __fastcall, getSaveDataFilePath, sigGetSaveDataFilePath(), prj::string& dstFilePath, const prj::string& fileName);
static FUNCTION_PTR(void, __fastcall, getSaveDataKey, sigGetSaveDataKey(), prj::string& dstKey, const prj::string& fileName, bool);

SIG_SCAN
(
    sigReadSaveData,
    0x1401D7C90,
    "\x48\x89\x5C\x24\x20\x55\x56\x57\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\x6C\x24\xD9\x48\x81\xEC\xE0\x00\x00\x00\x48\x8B\x05\xCC\xCC\xCC\xCC\x48\x33\xC4\x48\x89\x45\x17\x4D", 
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx"
);

SIG_SCAN
(
    sigWriteSaveData,
    0x1401D79D0,
    "\x40\x53\x55\x56\x57\x41\x54\x41\x57\x48\x83\xEC\x78",
    "xxxxxxxxxxxxx"
);

static FUNCTION_PTR(bool, __fastcall, readSaveData, sigReadSaveData(),
    const prj::string& fileName, prj::unique_ptr<uint8_t[]>& dst, size_t& dstSize);

static FUNCTION_PTR(bool, __fastcall, writeSaveData, sigWriteSaveData(),
    const prj::string& key, const uint8_t* src, size_t srcSize, prj::unique_ptr<uint8_t[]>& dst, size_t& dstSize);

SIG_SCAN
(
    sigLoadSaveData,
    0x1401D7FB0,
    "\x48\x85\xC9\x0F\x84\x75\x01",
    "xxxxxxx"
);

HOOK(void, __fastcall, LoadSaveData, sigLoadSaveData(), void* A1)
{
    originalLoadSaveData(A1);

    prj::unique_ptr<uint8_t[]> data;
    size_t dataSize = 0;

    if (!readSaveData(SaveDataEx::FILE_NAME, data, dataSize) || dataSize < (offsetof(SaveDataEx, headerSize) + sizeof(uint32_t)))
        return;

    const auto saveData = reinterpret_cast<SaveDataEx*>(data.get());

    if (saveData->headerSize > dataSize)
        return;

    for (uint32_t i = 0; i < saveData->scoreCount; i++)
    {
        auto& score = saveData->getScores()[i];
        scoreMap.insert(std::make_pair(score.pvId, score));
    }

    if (saveData->version >= 1)
    {
        for (uint32_t i = 0; i < saveData->moduleCount; i++)
        {
            auto& module = saveData->getModules()[i];
            moduleMap.insert(std::make_pair(module.moduleId, module.module));
        }
    }

    // Due to the faulty version checking in old DML versions, we cannot increase
    // the version number anymore without causing the extended save data to get erased
    // when the user downgrades their DML. Luckily, we can utilize the header size.
    if (saveData->headerSize > offsetof(SaveDataEx, cstmItemCount))
    {
        for (uint32_t i = 0; i < saveData->cstmItemCount; i++)
        {
            auto& cstmItem = saveData->getCstmItems()[i];
            cstmItemMap.insert(std::make_pair(cstmItem.cstmItemId, cstmItem.cstmItem));
        }
    }
}

SIG_SCAN
(
    sigSaveSaveData,
    0x1401D8280,
    "\x48\x85\xC9\x0F\x84\xDE", 
    "xxxxxx"
);

HOOK(void, __fastcall, SaveSaveData, sigSaveSaveData(), void* A1)
{
    originalSaveSaveData(A1);

    if (scoreMap.empty() && moduleMap.empty() && cstmItemMap.empty())
        return;

    std::vector<uint8_t> data(sizeof(SaveDataEx));

    const auto saveData = reinterpret_cast<SaveDataEx*>(data.data());
    saveData->version = SaveDataEx::MAX_VERSION;
    saveData->headerSize = sizeof(SaveDataEx);
    saveData->scoreCount = static_cast<uint32_t>(scoreMap.size());
    saveData->moduleCount = static_cast<uint32_t>(moduleMap.size());
    saveData->cstmItemCount = static_cast<uint32_t>(cstmItemMap.size());

    for (const auto& [pvId, score] : scoreMap)
    {
        const size_t offset = data.size();
        data.resize(offset + sizeof(Score));
        memcpy(&data[offset], &score, sizeof(Score));
    }

    for (const auto& [moduleId, module] : moduleMap)
    {
        const size_t offset = data.size();
        data.resize(offset + sizeof(ModuleEx));

        ModuleEx moduleEx =
        {
            moduleId, // moduleId
            module // module
        };
        memcpy(&data[offset], &moduleEx, sizeof(ModuleEx));
    }

    for (const auto& [cstmItemId, cstmItem] : cstmItemMap)
    {
        const size_t offset = data.size();
        data.resize(offset + sizeof(CstmItemEx));

        CstmItemEx cstmItemEx =
        {
            cstmItemId,
            cstmItem
        };
        memcpy(&data[offset], &cstmItemEx, sizeof(CstmItemEx));
    }

    prj::string filePath;
    prj::string key;

    getSaveDataFilePath(filePath, SaveDataEx::FILE_NAME);
    getSaveDataKey(key, SaveDataEx::FILE_NAME, true);

    prj::unique_ptr<uint8_t[]> fileData;
    size_t fileDataSize = 0;

    if (!writeSaveData(key, data.data(), data.size(), fileData, fileDataSize))
        return;

    FILE* file = fopen(filePath.c_str(), "wb");
    if (file != nullptr)
    {
        fwrite(fileData.get(), sizeof(uint8_t), fileDataSize, file);
        fclose(file);
    }
}

SIG_SCAN
(
    sigFindOrCreateScore,
    0x14E589750,
    "\x48\x83\xEC\x28\x49\x89\xCA\x85", 
    "xxxxxxxx"
);

SIG_SCAN
(
    sigFindScore,
    0x14E5A32F0,
    "\x85\xD2\x0F\x88\x7E", 
    "xxxxx"
);

SIG_SCAN
(
    sigFindModule,
    0x1401D5C90,
    "\x81\xFA\xFF\x03\x00\x00\x77",
    "xxxxxxx"
);

SIG_SCAN
(
    sigFindCstmItem,
    0x1401D5CB0,
    "\x81\xFA\xFF\x05\x00\x00\x77",
    "xxxxxxx"
);

SIG_SCAN
(
    sigFindCstmItemGallery,
    0x1401D5DD0,
    "\x81\xFA\xFF\x05\x00\x00\x76",
    "xxxxxxx"
);

// See SaveDataImp.asm for implementations.
HOOK(Score*, __fastcall, FindOrCreateScore, sigFindOrCreateScore(), void* A1, uint32_t pvId);
HOOK(Score*, __fastcall, FindScore, sigFindScore(), void* A1, uint32_t pvId);
HOOK(Module*, __fastcall, FindModule, sigFindModule(), void* A1, uint32_t moduleId);
HOOK(CstmItem*, __fastcall, FindCstmItem, sigFindCstmItem(), void* A1, uint32_t cstmItemId);
HOOK(CstmItem*, __fastcall, FindCstmItemGallery, sigFindCstmItemGallery(), void* A1, uint32_t cstmItemId);

extern uint8_t EMPTY_SCORE_DATA[];

std::set<uint32_t> BASE_SONGS = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, 28, 29, 30, 31, 32, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 79, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 101, 102, 103, 104, 201, 202, 203, 204, 205, 206, 208, 209, 210, 211, 212, 213, 214, 215, 216, 218, 219, 220, 221, 222, 223, 224, 225, 226, 227, 228, 231, 232, 233, 234, 235, 236, 238, 239, 240, 241, 242, 243, 244, 246, 247, 248, 249, 250, 251, 253, 254, 255, 257, 259, 260, 261, 262, 263, 265, 266, 267, 268, 269, 270, 271, 272, 273, 274, 275, 276, 277, 278, 279, 280, 281, 401, 402, 403, 404, 405, 407, 408, 409, 410, 411, 412, 413, 414, 415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429, 430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 600, 601, 602, 603, 604, 605, 607, 608, 609, 610, 611, 612, 613, 614, 615, 616, 617, 618, 619, 620, 621, 622, 623, 624, 625, 626, 627, 628, 629, 630, 631, 637, 638, 639, 640, 641, 642, 700, 701, 710, 722, 723, 724, 725, 726, 727, 728, 729, 730, 731, 732, 733, 734, 736, 737, 738, 739, 740, 832, 999};

Score* findOrCreateScoreImp(Score* A1, int32_t pvId)
{
    if (pvId >= 0 && BASE_SONGS.find(pvId) == BASE_SONGS.end())
    {
        const auto result = scoreMap.find(pvId);
        if (result != scoreMap.end())
            return &result->second;
    }

    Score* result = originalFindOrCreateScore(A1, pvId);

    if (result == nullptr && pvId >= 0)
    {
        // Don't put any base game songs into DML save
        if (BASE_SONGS.find(pvId) != BASE_SONGS.end())
        {
            // Doesn't need special accounting for SLP/Eden, will always be able to find a non-base slot within 300
            for (uint32_t i = 0; i < 300; i++)
            {
                if (BASE_SONGS.find(A1[i].pvId) == BASE_SONGS.end())
                {
                    result = &A1[i];
                    memcpy(&scoreMap[result->pvId], result, sizeof(Score));

                    const auto old = scoreMap.find(pvId);
                    if (old != scoreMap.end())
                    {
                        memcpy(result, &old->second, sizeof(Score));
                        scoreMap.erase(old);
                    }
                    else
                    {
                        memcpy(result, EMPTY_SCORE_DATA, sizeof(Score));
                        result->pvId = pvId;
                    }
                    break;
                }
            }
        }
        else
        {
            result = &scoreMap[pvId];
            memcpy(result, EMPTY_SCORE_DATA, sizeof(Score));
            result->pvId = pvId;
        }
    }

    return result;
}

Score* findScoreImp(void* A1, int32_t pvId)
{
    if (pvId >= 0)
    {
        const auto result = scoreMap.find(pvId);
        if (result != scoreMap.end())
            return &result->second;
    }

    return originalFindScore(A1, pvId);
}

Module* findModuleImp(void* A1, uint32_t moduleId)
{
    const auto pair = moduleMap.find(moduleId);
    if (pair != moduleMap.end())
        return &pair->second;

    Module* result = originalFindModule(A1, moduleId);

    if (result == nullptr)
    {
        auto& module = moduleMap[moduleId];
        module.unknown0 = 3;
        module.unknown1 = 0;

        result = &module;
    }

    return result;
}

CstmItem* findCstmItemImp(void* A1, uint32_t cstmItemId)
{
    const auto pair = cstmItemMap.find(cstmItemId);
    if (pair != cstmItemMap.end())
        return &pair->second;

    CstmItem* result = originalFindCstmItem(A1, cstmItemId);

    if (result == nullptr)
    {
        auto& cstmItem = cstmItemMap[cstmItemId];
        cstmItem.unknown0 = 3;

        result = &cstmItem;
    }

    return result;
}

void SaveData::init()
{
    INSTALL_HOOK(LoadSaveData);
    INSTALL_HOOK(SaveSaveData);
    INSTALL_HOOK(FindOrCreateScore);
    INSTALL_HOOK(FindScore);
    INSTALL_HOOK(FindModule);
    INSTALL_HOOK(FindCstmItem);
    INSTALL_HOOK(FindCstmItemGallery);
}
