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
    inline constexpr uint32_t k_scriptAbiVersion = 4u;

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

    /// @brief Script DLL と Engine で同じ配置を保つ 3 要素ベクトル
    struct ScriptVector3 final
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    /// @brief Script instance が値として保持するローカル Transform
    struct ScriptTransform final
    {
        ScriptVector3 position{};
        ScriptVector3 rotation{};
        ScriptVector3 scale{1.0f, 1.0f, 1.0f};
    };

    /// @brief GameScript DLL が所有する instance を識別する不透明 handle
    struct ScriptInstanceHandle final
    {
        uint64_t value = 0u;
    };

    /// @brief 無効な Script instance handle
    inline constexpr ScriptInstanceHandle k_invalidScriptInstanceHandle{};

    /// @brief Editor と GameScript が共有する公開 field の値種別
    enum class ScriptFieldType : uint32_t
    {
        Float = 0u,
        Int32,
        Bool,
        Entity,
        Script,
    };

    /// @brief 公開 field の保存と編集方法を指定する bit flag
    enum ScriptFieldFlags : uint32_t
    {
        ScriptFieldFlagNone = 0u,
        ScriptFieldFlagEditAnywhere = 1u << 0u,
        ScriptFieldFlagSerialize = 1u << 1u,
        ScriptFieldFlagReadOnly = 1u << 2u,
    };

    /// @brief DLL 境界で公開 field の metadata と値を受け渡す POD
    struct ScriptFieldValue final
    {
        ScriptStringView name{};
        ScriptStringView classValue{};
        ScriptEntityHandle entityValue{};
        ScriptFieldType type = ScriptFieldType::Float;
        uint32_t flags = ScriptFieldFlagNone;
        float floatValue = 0.0f;
        int32_t int32Value = 0;
        uint8_t boolValue = 0u;
        uint8_t reserved0 = 0u;
        uint8_t reserved1 = 0u;
        uint8_t reserved2 = 0u;
    };

    /// @brief reload 前後で state blob の互換性を判定する情報
    struct ScriptStateDescriptor final
    {
        uint32_t version = 1u;
        uint32_t size = 0u;
        uint64_t schemaHash = 0u;
    };

    struct ScriptEngineApi;

    /// @brief Entity が Runtime World 上で有効かを確認する callback
    using ScriptIsEntityValidFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity, uint8_t* a_outIsValid);

    /// @brief lifecycle 前に Runtime World の Transform を値型 cache へ同期する callback
    using ScriptReadTransformFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity,
        ScriptTransform* a_outTransform);

    /// @brief lifecycle 後に変更済みの値型 cache を Runtime World へ同期する callback
    using ScriptWriteTransformFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity,
        const ScriptTransform* a_transform);

    /// @brief Entity と class 名から現在の module instance を解決する callback
    using ScriptFindInstanceFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptEntityHandle a_entity,
        ScriptStringView a_className, ScriptInstanceHandle* a_outHandle);

    /// @brief reload や Component 変更後も instance handle が有効かを確認する callback
    using ScriptIsInstanceValidFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptInstanceHandle a_handle, uint8_t* a_outIsValid);

    /// @brief 登録済みの引数なし Script 関数を別 Script から呼び出す callback
    using ScriptInvokeFunctionFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_userData, ScriptInstanceHandle a_handle,
        ScriptStringView a_functionName);

    /// @brief Engine が GameScript DLL へ公開する最小操作一覧
    struct ScriptEngineApi final
    {
        uint32_t structSize = 0u;
        uint32_t abiVersion = k_scriptAbiVersion;
        void* userData = nullptr;
        ScriptIsEntityValidFn isEntityValid = nullptr;
        ScriptReadTransformFn readTransform = nullptr;
        ScriptWriteTransformFn writeTransform = nullptr;
        ScriptFindInstanceFn findInstance = nullptr;
        ScriptIsInstanceValidFn isInstanceValid = nullptr;
        ScriptInvokeFunctionFn invokeFunction = nullptr;
    };

    /// @brief Script instance の生成時に DLL へ渡す識別情報
    struct ScriptCreateInfo final
    {
        ScriptStringView className{};
        const ScriptFieldValue* fieldValues = nullptr;
        ScriptEntityHandle entity{};
        uint32_t fieldCount = 0u;
    };

    /// @brief Script instance の生成 callback
    using ScriptCreateStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        const ScriptEngineApi* a_engineApi, const ScriptCreateInfo* a_createInfo,
        void** a_outState);

    /// @brief Script instance の破棄 callback
    using ScriptDestroyStateFn = void(CUE_SCRIPT_CALL*)(void* a_state);

    /// @brief Script instance の初期化 callback
    using ScriptStartStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(void* a_state);

    /// @brief Script instance の更新 callback
    using ScriptUpdateStateFn =
        ScriptAbiResult(CUE_SCRIPT_CALL*)(void* a_state, float a_deltaTimeSeconds);

    /// @brief Script の reload 保存領域サイズを返す callback
    using ScriptGetStateSizeFn = uint32_t(CUE_SCRIPT_CALL*)();

    /// @brief Script の reload 保存領域へ状態を書き出す callback
    using ScriptSerializeStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        const void* a_state, void* a_outStateBuffer, uint32_t a_stateBufferSize);

    /// @brief reload 後の Script instance へ保存状態を復元する callback
    using ScriptRestoreStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_state, const void* a_stateBuffer, uint32_t a_stateBufferSize);

    /// @brief 公開 field の値を生成済み Script object に反映する callback
    using ScriptApplyFieldFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(
        void* a_state, const ScriptEngineApi* a_engineApi,
        const ScriptFieldValue* a_fieldValue);

    /// @brief 登録済みの引数なし Script 関数を呼び出す callback
    using ScriptInvokeStateFn = ScriptAbiResult(CUE_SCRIPT_CALL*)(void* a_state);
} // namespace Cue::Core::Native
