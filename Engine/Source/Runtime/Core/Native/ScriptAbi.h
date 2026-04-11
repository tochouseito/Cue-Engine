#pragma once

// === C++ includes ===
#include <cstdint>

// Script ABI の基本方針
// - 文字列は UTF-8 を前提とする
// - DLL 境界では所有権を跨がせない
// - DLL 境界では POD と関数ポインタだけを扱う
// - 例外は DLL 境界を跨がせない
// - このヘッダは C++ から利用するが、関数境界は C ABI に固定する

#if defined(_WIN32)
#if defined(CUE_SCRIPT_DLL_EXPORTS)
#define CUE_SCRIPT_EXPORT __declspec(dllexport)
#else
#define CUE_SCRIPT_EXPORT
#endif

#if defined(_MSC_VER)
#define CUE_SCRIPT_CALL __cdecl
#else
#define CUE_SCRIPT_CALL
#endif
#else
#define CUE_SCRIPT_EXPORT
#define CUE_SCRIPT_CALL
#endif

#ifdef __cplusplus
extern "C"
{
#endif

    inline constexpr uint32_t k_cueScriptAbiVersion = 1u;

    using CueScriptAbiVersion = uint32_t;

    enum CueResult : uint32_t
    {
        CueResult_Ok = 0,
        CueResult_InvalidArgument = 1,
        CueResult_NotFound = 2,
        CueResult_Unsupported = 3,
        CueResult_InvalidState = 4,
        CueResult_InternalError = 5,
    };

    enum CueLogSeverity : uint32_t
    {
        CueLogSeverity_Info = 0,
        CueLogSeverity_Warning = 1,
        CueLogSeverity_Error = 2,
    };

    /// @brief DLL 境界で使う UTF-8 文字列ビューです。
    struct CueStringView
    {
        const char* data;
        uint32_t size;
    };

    /// @brief Entity を表す外部公開ハンドルです。
    struct CueEntityHandle
    {
        uint64_t value;
    };

    /// @brief Component を表す外部公開ハンドルです。
    struct CueComponentHandle
    {
        uint64_t value;
    };

    /// @brief ScriptInstance を表す外部公開ハンドルです。
    struct CueScriptInstanceHandle
    {
        uint64_t value;
    };

    /// @brief 3 要素の float ベクトルです。
    struct CueFloat3
    {
        float x;
        float y;
        float z;
    };

    /// @brief Script 側へ公開する Transform データです。
    struct CueTransformData
    {
        CueFloat3 position;
        CueFloat3 rotation;
        CueFloat3 scale;
    };

    using CueLogFn = CueResult (CUE_SCRIPT_CALL*)(
        CueLogSeverity a_severity,
        CueStringView a_message
    );

    using CueIsEntityValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);
    using CueHasTransformFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);

    using CueGetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    using CueSetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Engine から Script へ渡す関数テーブルです。
    struct CueEngineApi
    {
        uint32_t structSize;
        CueScriptAbiVersion abiVersion;
        CueLogFn log;
        CueIsEntityValidFn isEntityValid;
        CueHasTransformFn hasTransform;
        CueGetTransformFn getTransform;
        CueSetTransformFn setTransform;
    };

    /// @brief Script インスタンス生成時の入力です。
    struct CueScriptCreateInfo
    {
        CueEntityHandle entityHandle;
        CueStringView scriptName;
    };

    using CueRegisterScriptsFn =
        CueResult (CUE_SCRIPT_CALL*)(const CueEngineApi* a_engineApi);

    using CueCreateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueScriptCreateInfo* a_createInfo,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    using CueDestroyScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    using CueUpdateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        float a_deltaTimeSeconds
    );

    /// @brief Script DLL が Engine へ返す関数テーブルです。
    struct CueScriptExports
    {
        uint32_t structSize;
        CueScriptAbiVersion abiVersion;
        CueRegisterScriptsFn registerScripts;
        CueCreateScriptInstanceFn createScriptInstance;
        CueDestroyScriptInstanceFn destroyScriptInstance;
        CueUpdateScriptInstanceFn updateScriptInstance;
    };

    /// @brief Script DLL の ABI version を返します。
    CUE_SCRIPT_EXPORT CueScriptAbiVersion CUE_SCRIPT_CALL
        cue_script_get_abi_version(void);

    /// @brief Script DLL の export table を返します。
    CUE_SCRIPT_EXPORT CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports
    );

#ifdef __cplusplus
}
#endif
