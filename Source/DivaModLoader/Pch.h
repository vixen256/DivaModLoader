#pragma once

// This is a trick to make the byte sizes of these types the same as the game.
#pragma push_macro("_ITERATOR_DEBUG_LEVEL")
#undef _ITERATOR_DEBUG_LEVEL
#define _ITERATOR_DEBUG_LEVEL 0

#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES

#include <Windows.h>
#include <detours.h>

#include <d3d11.h>
#include <d3d11_3.h>

#include <cassert>
#include <cstdint>
#include <cstdio>

#include <filesystem>
#include <list>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
#include <set>

#include <Helpers.h>
#include <toml.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/hwcontext_vulkan.h>
}

#include <dxvk/src/dxgi/dxgi_interfaces.h>

#include <volk.h>

#undef _ITERATOR_DEBUG_LEVEL
#pragma pop_macro("_ITERATOR_DEBUG_LEVEL")