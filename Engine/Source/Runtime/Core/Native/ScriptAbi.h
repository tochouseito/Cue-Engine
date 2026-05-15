#pragma once

// === C++ includes ===
#include <cstdint>

// Script ABI の基本方針
// - 文字列は UTF-8 を前提とする
// - DLL 境界では所有権を跨がせない
// - DLL 境界では POD と関数ポインタだけを扱う
// - 例外は DLL 境界を跨がせない
// - このヘッダは C++ から利用するが、関数境界は C ABI に固定する
    // - v2 ABI では Transform 操作、ログ出力、manual field 登録、
    //   EntityRef / ClassRef field を公開する
// - ABI 拡張は末尾追加のみとし、既存フィールドの順序変更や削除は行わない
// - `structSize` は呼び出し側が見えている構造体サイズを示す
// - 受け手は `structSize` を見て、既知範囲だけを安全に読む

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

    inline constexpr uint32_t k_cueScriptAbiVersion = 17u;
    inline constexpr uint64_t k_cueInvalidHandleValue = 0ull;
    inline constexpr uint64_t k_cueInvalidSceneId = 0ull;

    using CueScriptAbiVersion = uint32_t;
    using CueSceneId = uint64_t;

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
    /// `value == k_cueInvalidHandleValue` は無効値として扱います。
    struct CueEntityHandle
    {
        uint64_t value;
    };

    /// @brief Component を表す外部公開ハンドルです。
    /// 将来拡張向けに残している公開ハンドルです。
    struct CueComponentHandle
    {
        uint64_t value;
    };

    /// @brief ScriptInstance を表す外部公開ハンドルです。
    /// `value == k_cueInvalidHandleValue` は無効値として扱います。
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

    struct CueFloat4
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct CueQuaternion
    {
        float x;
        float y;
        float z;
        float w;
    };

    struct CueMouseDeltaData
    {
        int32_t x;
        int32_t y;
        int32_t wheel;
    };

    struct CueRaycastDesc
    {
        CueFloat3 origin;
        CueFloat3 direction;
        CueEntityHandle ignoredEntity;
        float distance;
    };

    struct CueRaycastHit
    {
        CueEntityHandle entity;
        CueFloat3 position;
        CueFloat3 normal;
        float distance;
    };

    /// @brief Script 側へ公開する Transform データです。
    /// Engine 内部の TransformComponent をそのまま露出せず、
    /// DLL 境界ではこの POD だけを受け渡します。
    struct CueTransformData
    {
        CueFloat3 position;
        /// @brief Euler 回転です。通常 API では弧度法、Degrees API では度数法です。
        CueFloat3 rotation;
        CueFloat3 scale;
    };

    struct CueTransformQuaternionData
    {
        CueFloat3 position;
        CueQuaternion rotation;
        CueFloat3 scale;
    };

    /// @brief Script から参照できるキーボードキーです。
    enum CueKey : uint32_t
    {
        CueKey_Unknown = 0,
        CueKey_Escape,
        CueKey_Tab,
        CueKey_CapsLock,
        CueKey_LeftShift,
        CueKey_RightShift,
        CueKey_LeftControl,
        CueKey_RightControl,
        CueKey_LeftAlt,
        CueKey_RightAlt,
        CueKey_Space,
        CueKey_Enter,
        CueKey_Backspace,
        CueKey_Insert,
        CueKey_Delete,
        CueKey_Home,
        CueKey_End,
        CueKey_PageUp,
        CueKey_PageDown,
        CueKey_Left,
        CueKey_Right,
        CueKey_Up,
        CueKey_Down,
        CueKey_Num0,
        CueKey_Num1,
        CueKey_Num2,
        CueKey_Num3,
        CueKey_Num4,
        CueKey_Num5,
        CueKey_Num6,
        CueKey_Num7,
        CueKey_Num8,
        CueKey_Num9,
        CueKey_A,
        CueKey_B,
        CueKey_C,
        CueKey_D,
        CueKey_E,
        CueKey_F,
        CueKey_G,
        CueKey_H,
        CueKey_I,
        CueKey_J,
        CueKey_K,
        CueKey_L,
        CueKey_M,
        CueKey_N,
        CueKey_O,
        CueKey_P,
        CueKey_Q,
        CueKey_R,
        CueKey_S,
        CueKey_T,
        CueKey_U,
        CueKey_V,
        CueKey_W,
        CueKey_X,
        CueKey_Y,
        CueKey_Z,
        CueKey_F1,
        CueKey_F2,
        CueKey_F3,
        CueKey_F4,
        CueKey_F5,
        CueKey_F6,
        CueKey_F7,
        CueKey_F8,
        CueKey_F9,
        CueKey_F10,
        CueKey_F11,
        CueKey_F12,
        CueKey_Count,
    };

    enum CueMouseButton : uint32_t
    {
        CueMouseButton_Left = 0,
        CueMouseButton_Right,
        CueMouseButton_Middle,
        CueMouseButton_X1,
        CueMouseButton_X2,
        CueMouseButton_Count,
    };

    /// @brief Script public field の型です。
    enum CueScriptFieldType : uint32_t
    {
        CueScriptFieldType_Float = 0,
        CueScriptFieldType_Int32 = 1,
        CueScriptFieldType_Bool = 2,
        CueScriptFieldType_EntityRef = 3,
        CueScriptFieldType_ClassRef = 4,
    };

    enum CueScriptFieldReferenceRole : uint32_t
    {
        CueScriptFieldReferenceRole_None = 0,
        CueScriptFieldReferenceRole_ScriptReferenceEntity = 1,
        CueScriptFieldReferenceRole_ScriptReferenceClass = 2,
    };

    enum CueScriptFieldFlags : uint32_t
    {
        CueScriptFieldFlag_None = 0,
        CueScriptFieldFlag_EditAnywhere = 1u << 0,
        CueScriptFieldFlag_Serialize = 1u << 1,
        CueScriptFieldFlag_ReadOnly = 1u << 2,
    };

    enum CueScriptFunctionFlags : uint32_t
    {
        CueScriptFunctionFlag_None = 0,
    };

    /// @brief DLL 境界で渡す Script public field 値です。
    struct CueScriptFieldValue
    {
        CueStringView name;
        CueScriptFieldType type;
        float floatValue;
        int32_t intValue;
        uint8_t boolValue;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
        CueEntityHandle entityValue;
        CueStringView classValue;
        CueStringView groupName;
        CueScriptFieldReferenceRole referenceRole;
        CueScriptFieldFlags flags;
    };

    struct CueScriptFunctionDefinition
    {
        CueStringView name;
        CueScriptFunctionFlags flags;
    };

    /// @brief Script state の互換性判定に使う署名です。
    struct CueScriptStateDescriptor
    {
        uint32_t stateVersion;
        uint32_t stateSize;
        uint64_t schemaHash;
    };

    using CueLogFn = CueResult (CUE_SCRIPT_CALL*)(
        CueLogSeverity a_severity,
        CueStringView a_message
    );

    /// @brief EntityHandle が現在も有効なら 1、無効なら 0 を返します。
    using CueIsEntityValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);
    /// @brief Transform が存在するなら 1、存在しないなら 0 を返します。
    using CueHasTransformFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);

    /// @brief Transform の値を取得します。
    /// `a_outTransform` は呼び出し側が所有し、確保も解放も呼び出し側が行います。
    using CueGetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    /// @brief Transform の値を設定します。
    /// `a_transform` が指すデータは呼び出し側所有で、呼び出し中だけ有効であれば十分です。
    using CueSetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Transform の値を度数法の回転で取得します。
    /// `position` と `scale` は通常の Transform と同じ単位です。
    using CueGetTransformDegreesFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    /// @brief Transform の値を度数法の回転で設定します。
    /// `position` と `scale` は通常の Transform と同じ単位です。
    using CueSetTransformDegreesFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Transform の値をクォータニオン回転で取得します。
    using CueGetTransformQuaternionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformQuaternionData* a_outTransform
    );

    /// @brief Transform の値をクォータニオン回転で設定します。
    using CueSetTransformQuaternionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformQuaternionData* a_transform
    );

    /// @brief 指定キーが押されていれば 1、そうでなければ 0 を返します。
    using CuePushKeyFn = uint8_t (CUE_SCRIPT_CALL*)(CueKey a_key);

    using CueGetMouseDeltaFn = CueResult (CUE_SCRIPT_CALL*)(
        CueMouseDeltaData* a_outDelta
    );

    using CuePushMouseButtonFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueMouseButton a_button);

    /// @brief Script DLL が利用可能な Script クラス名を Engine へ通知します。
    /// `a_scriptClassName` は UTF-8 の非所有文字列です。
    using CueRegisterScriptClassFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName
    );

    /// @brief Script クラスに属する public field を Engine へ通知します。
    using CueRegisterScriptFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue
    );

    using CueRegisterScriptFunctionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        const CueScriptFunctionDefinition* a_functionDefinition
    );

    /// @brief Entity に紐付いた ScriptInstance をクラス名で検索します。
    using CueFindScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueStringView a_scriptClassName,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    /// @brief ScriptInstanceHandle が現在も有効なら 1、無効なら 0 を返します。
    using CueIsScriptInstanceValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueScriptInstanceHandle a_instanceHandle);

    /// @brief Script public field 値を取得します。
    using CueGetScriptFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue
    );

    using CueGetScriptObjectFn =
        void* (CUE_SCRIPT_CALL*)(CueScriptInstanceHandle a_instanceHandle);

    /// @brief Script クラスが登録済みなら 1、未登録なら 0 を返します。
    using CueIsScriptClassRegisteredFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueStringView a_scriptClassName);

    /// @brief Script クラスに属する public field 定義を取得します。
    using CueGetScriptClassFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue
    );

    using CueHasScriptClassFunctionFn = uint8_t (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        CueStringView a_functionName
    );

    using CueInvokeScriptFunctionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_functionName
    );

    /// @brief Entity の AudioSourceComponent に再生 request を出します。
    using CueRequestAudioSourcePlayFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    /// @brief Scene 名から遅延ロードを予約し、予約された SceneId を返します。
    using CueRequestSceneLoadFn = CueSceneId (CUE_SCRIPT_CALL*)(
        CueStringView a_sceneName
    );

    /// @brief SceneId の遅延アンロードを予約します。
    using CueRequestSceneUnloadFn = CueResult (CUE_SCRIPT_CALL*)(
        CueSceneId a_sceneId
    );

    /// @brief Entity の RigidBodyComponent に線形速度を設定します。
    using CueSetRigidBodyLinearVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity
    );

    /// @brief Entity の RigidBodyComponent から線形速度を取得します。
    using CueGetRigidBodyLinearVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueFloat3* a_outVelocity
    );

    /// @brief Entity の RigidBodyComponent へ力を加えます。
    using CueAddRigidBodyForceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_force
    );

    /// @brief Entity の RigidBodyComponent へインパルスを加えます。
    using CueAddRigidBodyImpulseFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_impulse
    );

    /// @brief Entity の CharacterControllerComponent に水平移動速度を設定します。
    using CueSetCharacterMoveVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity
    );

    /// @brief Entity の CharacterControllerComponent にジャンプを要求します。
    using CueRequestCharacterJumpFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    /// @brief Entity の NavAgentComponent に目的地を設定します。
    using CueSetNavAgentDestinationFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_destination
    );

    /// @brief Entity の NavAgentComponent に追跡対象 Entity を設定します。
    using CueSetNavAgentTargetFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_targetEntityHandle
    );

    using CueRaycastFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueRaycastDesc* a_desc,
        CueRaycastHit* a_outHit
    );

    using CueDebugDrawLineFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueFloat3* a_start,
        const CueFloat3* a_end,
        const CueFloat4* a_color,
        float a_durationSeconds
    );

    using CueDebugDrawSphereFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueFloat3* a_center,
        float a_radius,
        const CueFloat4* a_color,
        float a_durationSeconds
    );

    using CueDebugDrawBoxFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueFloat3* a_center,
        const CueFloat3* a_halfExtent,
        const CueFloat4* a_color,
        float a_durationSeconds
    );

    /// @brief Engine から Script へ渡す関数テーブルです。
    /// 末尾拡張のみを許可します。
    struct CueEngineApi
    {
        /// 呼び出し側がコンパイル時に見えている `CueEngineApi` のサイズです。
        uint32_t structSize;
        /// 利用する ABI version です。現在は `k_cueScriptAbiVersion` を使います。
        CueScriptAbiVersion abiVersion;
        CueLogFn log;
        CueIsEntityValidFn isEntityValid;
        CueHasTransformFn hasTransform;
        CueGetTransformFn getTransform;
        CueSetTransformFn setTransform;
        /// v7 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CuePushKeyFn pushKey;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRegisterScriptClassFn registerScriptClass;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRegisterScriptFieldFn registerScriptField;
        /// v4 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRegisterScriptFunctionFn registerScriptFunction;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueFindScriptInstanceFn findScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueIsScriptInstanceValidFn isScriptInstanceValid;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptFieldFn getScriptField;
        /// v6 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptObjectFn getScriptObject;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueIsScriptClassRegisteredFn isScriptClassRegistered;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptClassFieldFn getScriptClassField;
        /// v4 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueHasScriptClassFunctionFn hasScriptClassFunction;
        /// v5 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueInvokeScriptFunctionFn invokeScriptFunction;
        /// v8 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRequestAudioSourcePlayFn requestAudioSourcePlay;
        /// v9 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRequestSceneLoadFn requestSceneLoad;
        /// v9 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRequestSceneUnloadFn requestSceneUnload;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetRigidBodyLinearVelocityFn setRigidBodyLinearVelocity;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetRigidBodyLinearVelocityFn getRigidBodyLinearVelocity;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueAddRigidBodyForceFn addRigidBodyForce;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueAddRigidBodyImpulseFn addRigidBodyImpulse;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetCharacterMoveVelocityFn setCharacterMoveVelocity;
        /// v10 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRequestCharacterJumpFn requestCharacterJump;
        /// v11 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetNavAgentDestinationFn setNavAgentDestination;
        /// v12 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetNavAgentTargetFn setNavAgentTarget;
        /// v13 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetMouseDeltaFn getMouseDelta;
        /// v13 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CuePushMouseButtonFn pushMouseButton;
        /// v14 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRaycastFn raycast;
        /// v15 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueDebugDrawLineFn debugDrawLine;
        /// v15 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueDebugDrawSphereFn debugDrawSphere;
        /// v15 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueDebugDrawBoxFn debugDrawBox;
        /// v16 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetTransformDegreesFn getTransformDegrees;
        /// v16 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetTransformDegreesFn setTransformDegrees;
        /// v17 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetTransformQuaternionFn getTransformQuaternion;
        /// v17 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSetTransformQuaternionFn setTransformQuaternion;
    };

    /// @brief Script インスタンス生成時の入力です。
    struct CueScriptCreateInfo
    {
        /// Script が紐付く Entity の外部公開ハンドルです。
        CueEntityHandle entityHandle;
        /// 生成対象 Script クラス名です。UTF-8、非所有です。
        CueStringView scriptName;
        /// Script public field 値配列です。null の場合は field なしとして扱います。
        const CueScriptFieldValue* fieldValues;
        /// `fieldValues` の要素数です。
        uint32_t fieldValueCount;
    };

    /// @brief Script DLL 側で利用可能な Script 種別を登録します。
    /// v1 では必須ではありませんが、将来の型レジストリ導入を見据えて残します。
    using CueRegisterScriptsFn =
        CueResult (CUE_SCRIPT_CALL*)(const CueEngineApi* a_engineApi);

    /// @brief ScriptInstance を生成します。
    /// 生成されたハンドルの実体所有権は Script DLL 側にあります。
    using CueCreateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueScriptCreateInfo* a_createInfo,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    /// @brief ScriptInstance を破棄します。
    /// 解放責務は生成した Script DLL 側にあります。
    using CueDestroyScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    /// @brief ScriptInstance を 1 フレーム分更新します。
    using CueUpdateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        float a_deltaTimeSeconds
    );

    /// @brief ScriptInstance の state サイズを返します。
    using CueGetScriptInstanceStateSizeFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        uint32_t* a_outStateSize
    );

    /// @brief ScriptInstance の state を呼び出し側バッファへ serialize します。
    using CueSerializeScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief ScriptInstance の state を復元します。
    using CueRestoreScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief ScriptInstance の実体 object pointer を返します。
    /// `nullptr` は未解決または無効を表します。
    using CueGetScriptInstanceObjectFn = void* (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    /// @brief Script クラスの state 署名を返します。
    using CueGetScriptStateDescriptorFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        CueScriptStateDescriptor* a_outDescriptor
    );

    /// @brief Script DLL が Engine へ返す関数テーブルです。
    /// v1 では末尾拡張のみを許可します。
    struct CueScriptExports
    {
        /// 呼び出し側がコンパイル時に見えている `CueScriptExports` のサイズです。
        uint32_t structSize;
        /// 利用する ABI version です。Engine と一致しない場合はロード失敗とします。
        CueScriptAbiVersion abiVersion;
        CueRegisterScriptsFn registerScripts;
        CueCreateScriptInstanceFn createScriptInstance;
        CueDestroyScriptInstanceFn destroyScriptInstance;
        CueUpdateScriptInstanceFn updateScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptInstanceStateSizeFn getScriptInstanceStateSize;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueSerializeScriptInstanceFn serializeScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueRestoreScriptInstanceFn restoreScriptInstance;
        /// v1 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptStateDescriptorFn getScriptStateDescriptor;
        /// v5 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueInvokeScriptFunctionFn invokeScriptFunction;
        /// v6 拡張です。`structSize` がこのメンバに届く場合だけ参照します。
        CueGetScriptInstanceObjectFn getScriptInstanceObject;
    };

    /// @brief Script DLL の ABI version を返します。
    /// この値は `k_cueScriptAbiVersion` と一致している必要があります。
    CUE_SCRIPT_EXPORT CueScriptAbiVersion CUE_SCRIPT_CALL
        cue_script_get_abi_version(void);

    /// @brief Script DLL の export table を返します。
    /// `a_outExports` は呼び出し側が確保し、呼び出し前に null でないことが必須です。
    CUE_SCRIPT_EXPORT CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports
    );

#ifdef __cplusplus
}
#endif
