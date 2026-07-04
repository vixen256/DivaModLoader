#include "ModLoader.h"

#include "CodeLoader.h"
#include "Context.h"
#include "DatabaseLoader.h"
#include "SigScan.h"
#include "Types.h"
#include "Utilities.h"

SIG_SCAN
(
    sigInitRomDirectoryPaths,
    0x1402A23E0,
    "\x48\x89\x5C\x24\x08\x48\x89\x74\x24\x10\x48\x89\x7C\x24\x18\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8B\xEC\x48\x81\xEC\x80\x00\x00\x00\x48\x8B\x05\xCC\xCC\xCC\xCC\x48\x33\xC4\x48\x89\x45\xF0\x48", 
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx"
);

HOOK(void, __fastcall, InitRomDirectoryPaths, sigInitRomDirectoryPaths())
{
    originalInitRomDirectoryPaths();

    // Get the address of the vector from the lea instruction that loads it.
    const auto romDirectoryPaths = (prj::vector<prj::string>*)(readInstrPtr(sigInitRomDirectoryPaths(), 0x30, 0x7));

    std::vector<std::string> modRomDirectoryPaths;

    // Check for every rom folder within mod directory paths.
    for (auto& modDirectoryPath : ModLoader::modDirectoryPaths)
    {
        for (auto& romDirectoryPath : *romDirectoryPaths)
            modRomDirectoryPaths.push_back(modDirectoryPath + "\\" + romDirectoryPath.c_str());
    }

    // Cleanse directories that do not exist.
    processDirectoryPaths(modRomDirectoryPaths, false);

    if (modRomDirectoryPaths.empty())
        return;

    LOG("ROM:")

    for (auto& modRomDirectoryPath : modRomDirectoryPaths)
        LOG(" - %s", modRomDirectoryPath.c_str());

    // Insert to the beginning of the game's rom directory paths.
    // The first item in the vector has the highest priority.
    romDirectoryPaths->insert(romDirectoryPaths->begin(), modRomDirectoryPaths.begin(), modRomDirectoryPaths.end());

    // Initialize mount data manager prefixes for mod databases.
    DatabaseLoader::initMdataMgr(modRomDirectoryPaths);
}

std::vector<std::string> ModLoader::modDirectoryPaths;
std::set<std::string> modNames;

void ModLoader::initMod(const std::filesystem::path& path)
{
    const std::string modDirectoryPath = path.string();
    const std::string configFilePath = modDirectoryPath + "\\config.toml";

    toml::table config;

    try
    {
        config = toml::parse_file(configFilePath);
    }

    catch (std::exception& exception)
    {
        LOG(" - Failed to load \"%s\": %s", getRelativePath(configFilePath).c_str(), exception.what())
        return;
    }

    if (!config["enabled"].value_or(true))
        return;

    const std::string modName = config["name"].value_or(path.filename().string());
    modNames.insert(modName);
    LOG(" - %s", modName.c_str())

    if (toml::array* includeArr = config["include"].as_array())
    {
        toml::array nodes;
        for (size_t i = includeArr->size() - 1; i != -1; i--)
            nodes.push_back(includeArr->at(i));

        while (!nodes.empty())
        {
            toml::array temp;
            temp.push_back(nodes.back());
            nodes.pop_back();
            toml::node& elem = temp.back();

            if (toml::value<std::string>* include = elem.as_string())
            {
                if (!(*include)->empty())
                    modDirectoryPaths.push_back(modDirectoryPath + "\\" + **include);
            }
            else if (toml::table* includeTable = elem.as_table())
            {
                bool enabled = includeTable->at_path("enabled").value_or(true);

                if (toml::array* arr = includeTable->at_path("requires").as_array())
                {
                    for (size_t i = arr->size() - 1; i != -1; i--)
                    {
                        toml::value<std::string>* name = arr->at(i).as_string();
                        if (!name || (*name)->empty())
                            continue;

                        if (modNames.find(**name) == modNames.end())
                        {
                            const std::string includeName = modName + " - " + includeTable->at_path("name").value_or(includeTable->at_path("include").as_string()->value_or("Unknown"));
                            const std::wstring msg = L"Failed to load \"" + std::wstring(includeName.begin(), includeName.end()) + L"\"\n" + L"Could not find dependency \"" + std::wstring((*name)->begin(), (*name)->end()) + L"\"";
                            MessageBoxW(nullptr, msg.c_str(), L"DIVA Mod Loader", MB_OK);
                            enabled = false;
                            break;
                        }
                    }
                }

                if (toml::array* arr = includeTable->at_path("conflicts").as_array())
                {
                    for (size_t i = arr->size() - 1; i != -1; i--)
                    {
                        toml::value<std::string>* name = arr->at(i).as_string();
                        if (!name || (*name)->empty())
                            continue;

                        if (modNames.find(**name) != modNames.end())
                        {
                            const std::string includeName = modName + " - " + includeTable->at_path("name").value_or(includeTable->at_path("include").as_string()->value_or("Unknown"));
                            const std::wstring msg = L"Failed to load \"" + std::wstring(includeName.begin(), includeName.end()) + L"\"\n" + L"Found conflict \"" + std::wstring((*name)->begin(), (*name)->end()) + L"\"";
                            MessageBoxW(nullptr, msg.c_str(), L"DIVA Mod Loader", MB_OK);
                            enabled = false;
                            break;
                        }
                    }
                }

                if (!enabled)
                    continue;

                if (toml::value<std::string>* name = includeTable->at_path("name").as_string())
                    if (!(*name)->empty())
                        LOG(" - %s - %s", modName.c_str(), (*name)->c_str())

                if (toml::value<std::string>* include = includeTable->at_path("include").as_string())
                    if (!(*include)->empty())
                        modDirectoryPaths.push_back(modDirectoryPath + "\\" + **include);
                else if (toml::array* arr = includeTable->at_path("include").as_array())
                    for (size_t i = arr->size() - 1; i != -1; i--)
                        nodes.push_back(arr->at(i));
            }
        }
    }

    if (toml::array* dllArr = config["dll"].as_array())
    {
        for (auto& dllElem : *dllArr)
        {
            const std::string dll = dllElem.value_or("");

            if (!dll.empty())
                CodeLoader::dllFilePaths.push_back(path.wstring() + L"\\" + convertMultiByteToWideChar(dll));
        }
    }
}

void ModLoader::init()
{
    LOG("Mods: \"%s\"", getRelativePath(Config::modsDirectoryPath).c_str())

    if (!Config::priorityPaths.empty())
    {
        LOG(" Using priority array")

        for (auto& path : Config::priorityPaths)
        {
            const std::string modDirectory = Config::modsDirectoryPath + "\\" + path;
            if (std::filesystem::is_directory(modDirectory))
                initMod(modDirectory);
        }
    }
    else
    {
        LOG(" Using alphanumeric folder name order for priority")

        for (auto& modDirectory : std::filesystem::directory_iterator(Config::modsDirectoryPath))
        {
            if (std::filesystem::is_directory(modDirectory))
                initMod(modDirectory.path());
        }
    }
    if (!modDirectoryPaths.empty())
        INSTALL_HOOK(InitRomDirectoryPaths);
}