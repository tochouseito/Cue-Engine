#pragma once
#define WIN32_LEAN_AND_MEAN             // windows ヘッダー軽量化
#define NOMINMAX                        // min と max マクロ抑止
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <process.h>
#include <new>
#include <timeapi.h>
#include <wrl.h>
#ifdef CUE_DEBUG
#include <debugapi.h>
#endif
