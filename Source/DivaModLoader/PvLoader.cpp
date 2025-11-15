#include "PvLoader.h"
#include "SigScan.h"
#include "Types.h"

SIG_SCAN
(
    sigTaskPvDbLoop,
    0x1404BB290,
    "\x48\x89\x5C\x24\x10\x48\x89\x74\x24\x18\x48\x89\x7C\x24\x20\x55\x41\x54\x41\x55\x41\x56\x41\x57\x48\x8D\xAC\x24\x70\xFC",
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
);

std::list<void*> fileHandlers;

static FUNCTION_PTR(bool, __fastcall, asyncFileLoad, 0x1402A4710, void** fileHandler, const char* file, bool);
static FUNCTION_PTR(bool, __fastcall, asyncFileLoading, 0x151C03830, void** fileHandler);
static FUNCTION_PTR(void, __fastcall, freeAsyncFileHandler, 0x1402A4E90, void** fileHandler);

HOOK(bool, __fastcall, TaskPvDbLoop, sigTaskPvDbLoop(), uint64_t task) {
    auto state = (int*)(task + 0x68);
    auto paths = (prj::list<prj::string>*)(task + 0x70);
    auto pvData = (prj::list<void*>*)(task + 0x88);
    auto reset = (bool*)(task + 0xB0);

    if (*reset)
    {
        paths->clear();
        pvData->clear();
        *reset = false;
        *state = 0;
    }
    else if (*state == 0)
    {
        if (paths->size() > 0)
        {
            for (auto it = paths->begin(); it != paths->end(); it++)
            {
                void* handler = nullptr;
                asyncFileLoad(&handler, it->c_str(), false);
                fileHandlers.push_back(handler);
            }

            *state = 1;
        }
    }
    else
    {
        while (fileHandlers.size() > 0)
        {
            auto handler = fileHandlers.front();
            if (asyncFileLoading(&handler))
                break;

            *state = 3;
            *(void**)(task + 0x80) = handler;
            originalTaskPvDbLoop(task);

            freeAsyncFileHandler(&handler);
            fileHandlers.pop_front();
            paths->pop_front();
        }
    }

    return false;
}

SIG_SCAN
(
    sigPvLoaderParseStart,
    0x1404BB3C1,
    "\x49\xBD\xEB\x68\xF3\x3E\xC5\x25\x43\x00\x0F\x1F\x44\x00\x00", 
    "xxxxxxxxxxxxxxx"
);

HOOK(void, __fastcall, PvLoaderParseStart, sigPvLoaderParseStart());

static std::vector<uint32_t> pvIdStack;

uint32_t pvLoaderParseStartImp(const char* data, size_t length)
{
    size_t i = 0;
    uint32_t lastPvId = 0;
    while (i < length) 
    {
        // Skip whitespace at the start of line.
        while (i < length && (data[i] == '\t' || data[i] == '\n' || data[i] == '\r' || data[i] == ' '))
            i++;

        if (length - i > 3 && data[i] == 'p' && data[i + 1] == 'v' && data[i + 2] == '_') 
        {
            i += 3;

            uint32_t pvId = 0;
            while(i < length && std::isdigit(data[i]))
            {
                pvId *= 10;
                pvId += data[i] - '0';
                i++;
            }

            if (pvId != lastPvId && pvId != 0)
            {
                lastPvId = pvId;
                pvIdStack.push_back(pvId);
            }
        }

        // Move onto the next line.
        while (i < length && data[i] != '\n' && data[i] != '\r')
            i++;
    }

    if (!pvIdStack.empty())
    {
        uint32_t pvId = pvIdStack.back();
        pvIdStack.pop_back();
        return pvId;
    }

    return 0xFFFFFFFF;
}

SIG_SCAN
(
    sigPvLoaderParseLoop,
    0x1404BB5D6,
    "\x49\xFF\xC6\x49\x81\xFE\xE8\x03\x00\x00", 
    "xxxxxxxxxx"
);

HOOK(void, __fastcall, PvLoaderParseLoop, sigPvLoaderParseLoop());

uint32_t pvLoaderParseLoopImp()
{
    if (!pvIdStack.empty()) 
    {
        uint32_t pvId = pvIdStack.back();
        pvIdStack.pop_back();
        return pvId;
    }
    
    return 0xFFFFFFFF;
}

SIG_SCAN
(
    sigPvLoaderIfCheck1,
    0x1405807C2,
    "\x80\xBC\x02\x40\x03\x00\x00\x00", 
    "xxxxxxxx"
);

SIG_SCAN
(
    sigPvLoaderIfCheck2,
    0x1405807F0,
    "\x44\x38\x08\x75\x11", 
    "xxxxx"
);

SIG_SCAN
(
    sigPvLoaderIfCheck3,
    0x1405811E0,
    "\x80\x38\x00\x75\x4C", 
    "xxxxx"
);

SIG_SCAN
(
    sigPvLoaderU16Trunc1,
    0x1406DE563,
    "\x66\x44\x89\xB1\x88\x0A\x00\x00",
    "xxxxxxxx"
);

SIG_SCAN
(
    sigPvLoaderU16Trunc2,
    0x1406DEA21,
    "\x66\x89\x91\x88\x0A\x00\x00", 
    "xxxxxxx"
);

SIG_SCAN
(
    sigPvLoaderU16Trunc3,
    0x1406DF6F4,
    "\x0F\xBF\x91\x88\x0A\x00\x00", 
    "xxxxxxx"
);

SIG_SCAN
(
    sigPvLoaderU16Trunc4,
    0x1406DF748,
    "\x66\x83\xB9\x88\x0A\x00\x00\xFF", 
    "xxxxxxxx"
);

void PvLoader::init()
{
    INSTALL_HOOK(TaskPvDbLoop);

    // Skip if checks that always return true but would access out of bounds data due to large IDs regardless
    WRITE_NOP(sigPvLoaderIfCheck1(), 0xE);
    WRITE_MEMORY(sigPvLoaderIfCheck2(), uint8_t, 0x90, 0x90, 0x90, 0xEB);
    WRITE_MEMORY((uint8_t*)sigPvLoaderIfCheck2() + 0x23, uint8_t, 0x90, 0x90, 0x90, 0xEB); // 0x140580813
    WRITE_MEMORY((uint8_t*)sigPvLoaderIfCheck2() + 0x46, uint8_t, 0x90, 0x90, 0x90, 0xEB); // 0x140580836
    WRITE_MEMORY((uint8_t*)sigPvLoaderIfCheck2() + 0x70, uint8_t, 0x90, 0x90, 0x90, 0xEB); // 0x140580860
    WRITE_MEMORY((uint8_t*)sigPvLoaderIfCheck2() + 0x93, uint8_t, 0x90, 0x90, 0x90, 0xEB); // 0x140580883
    WRITE_MEMORY(sigPvLoaderIfCheck3(), uint8_t, 0x90, 0x90, 0x90, 0xEB);

    // Prevent truncation to u16 when using MM+ UI, there's enough space for an u32 since the next element is 4 byte aligned
    WRITE_NOP(sigPvLoaderU16Trunc1(), 1);
    WRITE_NOP(sigPvLoaderU16Trunc2(), 1);
    WRITE_NOP((uint8_t*)sigPvLoaderU16Trunc2() + 7, 3);
    WRITE_MEMORY(sigPvLoaderU16Trunc3(), uint8_t, 0x90, 0x8B);
    WRITE_NOP(sigPvLoaderU16Trunc4(), 1);

    // Scan the pv_db file before reading it to not waste time looking for entries that don't exist in the file
    WRITE_CALL(originalPvLoaderParseStart, implOfPvLoaderParseStart);
    WRITE_NOP(reinterpret_cast<uint8_t*>(originalPvLoaderParseStart) + 0xC, 0x3);
    
    WRITE_JUMP(originalPvLoaderParseLoop, implOfPvLoaderParseLoop);
}
