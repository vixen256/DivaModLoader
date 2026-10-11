#pragma once

extern void* sigAsyncFileLoad();
extern void* sigAsyncFileLoading();
extern void* sigAsyncFileGetData();
extern void* sigAsyncFileGetSize();
extern void* sigFreeAsyncFileHandler();

static inline FUNCTION_PTR(bool, __fastcall, asyncFileLoad, sigAsyncFileLoad(), void**, const char*, int);
static inline FUNCTION_PTR(bool, __fastcall, asyncFileLoading, sigAsyncFileLoading(), void**);
static inline FUNCTION_PTR(const void*, __fastcall, asyncFileGetData, sigAsyncFileLoading(), void**);
static inline FUNCTION_PTR(size_t, __fastcall, asyncFileGetSize, sigAsyncFileGetSize(), void**);
static inline FUNCTION_PTR(void, __fastcall, freeAsyncFileHandler, sigFreeAsyncFileHandler(), void**);

class FileLoader
{
public:
    static void init();
};
