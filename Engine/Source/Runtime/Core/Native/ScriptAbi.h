#pragma once

/// **********************************************************************
/// GameScript DLL と Engine 間で共有する最小 Script ABI を定義する
/// **********************************************************************

// === C++ includes ===
#include <cstdint>

#if defined(_WIN32)
// DLL 境界の呼び出し規約を固定し、Engine と GameScript の stack を一致させる
#ifndef CUE_SCRIPT_CALL
#define CUE_SCRIPT_CALL __cdecl
#endif
#else
#ifndef CUE_SCRIPT_CALL
#define CUE_SCRIPT_CALL
#endif
#endif

namespace Cue::Core::Native
{
    /// @brief Engine と GameScript が共有する最小 ABI 世代
    inline constexpr uint32_t k_scriptAbiVersion = 1u;

    /// @brief DLL 境界で Engine 操作の結果を伝える値
    enum class ScriptAbiResult : uint32_t
    {
        Ok = 0u,
        InvalidArgument,
        NotFound,
        InvalidState,
        InternalError,
    };

    /// @brief DLL 境界で UTF-8 文字列を所有権なしに参照する値
    struct ScriptStringView final
    {
        const char* data = nullptr;
        uint32_t size = 0u;
    };

    /// @brief Entity の再利用を検出するための Script 用 handle
    struct ScriptEntityHandle final
    {
        uint32_t entityId = 0u;
        uint32_t generation = 0u;
    };

    /// @brief Script DLL と Engine で同じ配置を保つ quaternion
    struct ScriptQuaternion final
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    /// @brief Script から読み書きするローカル Transform
    struct ScriptTransformQuaternion final
    {
        float positionX = 0.0f;
        float positionY = 0.0f;
        float positionZ = 0.0f;
        ScriptQuaternion rotation{};
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float scaleZ = 1.0f;
    };

    /// @brief GameScript DLL が所有する instance を識別する不透明 handle
    struct ScriptInstanceHandle final
    {
        uint64_t value = 0u;
    };

    /// @brief 無効な Script instance handle
    inline constexpr ScriptInstanceHandle k_invalidScriptInstanceHandle{};

    struct ScriptEngineApi;

    /// @brief Entity が Runtime World 上で有効かを確認する callback
    using ScriptIsEntityValidFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity, uint8_t* a_outIsValid);

    /// @brief Entity のローカル Transform を quaternion 回転で取得する callback
    using ScriptGetTransformQuaternionFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity,
        ScriptTransformQuaternion* a_outTransform);

    /// @brief Entity のローカル Transform を quaternion 回転で更新する callback
    using ScriptSetTransformQuaternionFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity,
        const ScriptTransformQuaternion* a_transform);

    /// @brief Engine が GameScript DLL へ公開する最小操作一覧
    struct ScriptEngineApi final
    {
        uint32_t structSize = 0u;
        uint32_t abiVersion = k_scriptAbiVersion;
        void* userData = nullptr;
        ScriptIsEntityValidFn isEntityValid = nullptr;
        ScriptGetTransformQuaternionFn getTransformQuaternion = nullptr;
        ScriptSetTransformQuaternionFn setTransformQuaternion = nullptr;
    };

    /// @brief Script instance の生成時に DLL へ渡す識別情報
    struct ScriptCreateInfo final
    {
        ScriptEntityHandle entity{};
        ScriptStringView className{};
    };

    /// @brief Script instance の生成 callback
    using ScriptCreateStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        const ScriptEngineApi* a_engineApi, const ScriptCreateInfo* a_createInfo,
        void** a_outState);

    /// @brief Script instance の破棄 callback
    using ScriptDestroyStateFn = void(CUE_SCRIPT_CALL*)(void* a_state);

    /// @brief Script instance の更新 callback
    using ScriptUpdateStateFn =
        ScriptAbiResult(CUE_SCRIPT_CALL*)(void* a_state, float a_deltaTimeSeconds);
} // namespace Cue::Core::Native
