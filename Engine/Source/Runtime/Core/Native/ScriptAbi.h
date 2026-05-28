// ScriptAbi の役割と公開要素を定義する

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

    inline constexpr uint32_t k_cueScriptAbiVersion = 25u;
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

    /// @brief DLL 境界で使う UTF-8 文字列ビュー
    struct CueStringView
    {
        const char* data;
        uint32_t size;
    };

    /// @brief Entity を表す外部公開ハンドル
    /// `value == k_cueInvalidHandleValue` は無効値として扱う
    struct CueEntityHandle
    {
        uint64_t value;
    };

    /// @brief Script から参照する JSON 設定ハンドル
    struct CueJsonConfigHandle
    {
        uint32_t index;
        uint32_t generation;
    };

    inline constexpr CueJsonConfigHandle k_cueInvalidJsonConfigHandle{
        0xffffffffu,
        0u
    };

    /// @brief Component を表す外部公開ハンドル
    /// 将来拡張向けに残している公開ハンドル
    struct CueComponentHandle
    {
        uint64_t value;
    };

    /// @brief ScriptInstance を表す外部公開ハンドル
    /// `value == k_cueInvalidHandleValue` は無効値として扱う
    struct CueScriptInstanceHandle
    {
        uint64_t value;
    };

    /// @brief 2 要素の float ベクトル
    struct CueFloat2
    {
        float x;
        float y;
    };

    /// @brief 3 要素の float ベクトル
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

    enum CueSpawnObjectKind : uint32_t
    {
        CueSpawnObjectKind_Empty = 0,
        CueSpawnObjectKind_StaticMesh = 1,
        CueSpawnObjectKind_Sprite = 2,
        CueSpawnObjectKind_Camera = 3,
        CueSpawnObjectKind_DirectionalLight = 4,
        CueSpawnObjectKind_PointLight = 5,
        CueSpawnObjectKind_SpotLight = 6,
    };

    enum CueScriptCollisionEventType : uint32_t
    {
        CueScriptCollisionEventType_Enter = 0,
        CueScriptCollisionEventType_Stay = 1,
        CueScriptCollisionEventType_Exit = 2,
    };

    enum CueComponentKind : uint32_t
    {
        CueComponentKind_Camera = 0,
        CueComponentKind_Collider = 1,
        CueComponentKind_TriggerVolume = 2,
        CueComponentKind_MeshFilter = 3,
        CueComponentKind_StaticMeshRenderer = 4,
        CueComponentKind_SpriteRenderer = 5,
        CueComponentKind_Canvas = 6,
        CueComponentKind_UiRectTransform = 7,
        CueComponentKind_UiLayoutGroup = 8,
        CueComponentKind_TextRenderer = 9,
        CueComponentKind_UiImage = 10,
        CueComponentKind_UiButton = 11,
        CueComponentKind_UiCheckbox = 12,
        CueComponentKind_UiSlider = 13,
    };

    enum CueUiLayoutDirection : uint32_t
    {
        CueUiLayoutDirection_Horizontal = 0,
        CueUiLayoutDirection_Vertical = 1,
    };

    enum CueTextHorizontalAlign : uint32_t
    {
        CueTextHorizontalAlign_Left = 0,
        CueTextHorizontalAlign_Center = 1,
        CueTextHorizontalAlign_Right = 2,
    };

    enum CueTextVerticalAlign : uint32_t
    {
        CueTextVerticalAlign_Top = 0,
        CueTextVerticalAlign_Middle = 1,
        CueTextVerticalAlign_Bottom = 2,
    };

    enum CueShadowCasterMode : uint8_t
    {
        CueShadowCasterMode_Solid = 0,
        CueShadowCasterMode_TwoSided = 1,
    };

    enum CueMaterialPropertyOverride : uint32_t
    {
        CueMaterialPropertyOverride_Color = 1u << 0,
        CueMaterialPropertyOverride_Shininess = 1u << 1,
        CueMaterialPropertyOverride_ReflectionSkybox = 1u << 2,
    };

    enum CueColliderShapeType : uint32_t
    {
        CueColliderShapeType_Box = 0,
        CueColliderShapeType_Sphere = 1,
        CueColliderShapeType_Capsule = 2,
        CueColliderShapeType_Mesh = 3,
    };

    /// @brief Script 側へ公開する Transform データ
    /// Engine 内部の TransformComponent をそのまま露出せず、
    /// DLL 境界ではこの POD だけを受け渡し
    struct CueTransformData
    {
        CueFloat3 position;
        /// @brief Euler 回転通常 API では弧度法、Degrees API では度数法
        CueFloat3 rotation;
        CueFloat3 scale;
    };

    struct CueTransformQuaternionData
    {
        CueFloat3 position;
        CueQuaternion rotation;
        CueFloat3 scale;
    };

    struct CueSpawnObjectDesc
    {
        CueSpawnObjectKind kind;
        CueSceneId sceneId;
        CueStringView name;
        CueStringView tag;
        CueTransformData transform;
        uint8_t isActive;
        uint8_t isPersistent;
        uint8_t reserved0;
        uint8_t reserved1;
    };

    struct CueInstantiateEntityDesc
    {
        CueEntityHandle sourceEntity;
        CueStringView name;
        CueStringView tag;
        CueTransformData transform;
        uint8_t usesName;
        uint8_t usesTag;
        uint8_t usesTransform;
        uint8_t usesActive;
        uint8_t isActive;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueSphereOverlapDesc
    {
        CueFloat3 center;
        CueEntityHandle ignoredEntity;
        float radius;
        uint8_t includeTriggers;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueCameraComponentData
    {
        float fovY;
        float aspectRatio;
        float nearZ;
        float farZ;
        uint8_t isMain;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueColliderComponentData
    {
        CueStringView meshModelName;
        CueFloat3 offset;
        CueFloat3 halfExtent;
        CueColliderShapeType shapeType;
        float radius;
        float halfHeight;
        float friction;
        float restitution;
        uint16_t layer;
        uint16_t mask;
        uint8_t isTrigger;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueTriggerVolumeComponentData
    {
        uint8_t includeTriggers;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueMeshFilterComponentData
    {
        CueStringView modelName;
        uint32_t meshId;
    };

    struct CueStaticMeshRendererComponentData
    {
        uint8_t visible;
        uint8_t castsShadow;
        uint8_t receivesShadow;
        uint8_t shadowCasterMode;
    };

    struct CueMaterialPropertyBlockData
    {
        CueFloat4 color;
        float shininess;
        uint32_t overrideMask;
        uint8_t usesReflectionSkybox;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueSpriteRendererComponentData
    {
        CueFloat4 color;
        CueFloat4 uvRect;
        CueFloat2 size;
        CueFloat2 pivot;
        int32_t layer;
        uint32_t order;
        uint8_t isVisible;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueCanvasComponentData
    {
        CueFloat2 referenceSize;
        float scaleFactor;
        int32_t sortOrder;
        uint8_t matchesScreen;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueUiRectTransformComponentData
    {
        CueFloat2 anchorMin;
        CueFloat2 anchorMax;
        CueFloat2 pivot;
        CueFloat2 anchoredPosition;
        CueFloat2 sizeDelta;
    };

    struct CueUiLayoutGroupComponentData
    {
        CueFloat4 padding;
        float spacing;
        CueUiLayoutDirection direction;
        uint8_t controlsChildSize;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueTextRendererComponentData
    {
        CueStringView text;
        CueStringView fontPath;
        CueFloat4 color;
        uint32_t fontSize;
        int32_t layer;
        uint32_t order;
        CueTextHorizontalAlign horizontalAlign;
        CueTextVerticalAlign verticalAlign;
        uint8_t visible;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueUiImageComponentData
    {
        CueFloat4 color;
        CueFloat4 uvRect;
        int32_t layer;
        uint32_t order;
        uint8_t visible;
        uint8_t raycastTarget;
        uint8_t reserved0;
        uint8_t reserved1;
    };

    struct CueUiButtonComponentData
    {
        CueFloat4 normalColor;
        CueFloat4 hoverColor;
        CueFloat4 pressedColor;
        CueFloat4 disabledColor;
        int32_t layer;
        uint32_t order;
        uint8_t isInteractable;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueUiButtonStateData
    {
        uint8_t isHovered;
        uint8_t isPressed;
        uint8_t wasClicked;
        uint8_t hasFocus;
    };

    struct CueUiCheckboxComponentData
    {
        CueFloat4 normalColor;
        CueFloat4 hoverColor;
        CueFloat4 checkColor;
        CueFloat4 disabledColor;
        int32_t layer;
        uint32_t order;
        uint8_t isInteractable;
        uint8_t isChecked;
        uint8_t reserved0;
        uint8_t reserved1;
    };

    struct CueUiCheckboxStateData
    {
        uint8_t isChecked;
        uint8_t isHovered;
        uint8_t isPressed;
        uint8_t wasChanged;
        uint8_t hasFocus;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueUiSliderComponentData
    {
        CueFloat4 trackColor;
        CueFloat4 fillColor;
        CueFloat4 handleColor;
        CueFloat4 disabledColor;
        float minValue;
        float maxValue;
        float value;
        int32_t layer;
        uint32_t order;
        uint8_t isInteractable;
        uint8_t reserved0;
        uint8_t reserved1;
        uint8_t reserved2;
    };

    struct CueUiSliderStateData
    {
        float value;
        uint8_t isHovered;
        uint8_t isDragging;
        uint8_t wasChanged;
        uint8_t hasFocus;
    };

    /// @brief Script から参照できるキーボードキー
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

    /// @brief Script public field の型
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

    /// @brief DLL 境界で渡す Script public field 値
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

    /// @brief Script state の互換性判定に使う署名
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

    /// @brief EntityHandle が現在も有効なら 1、無効なら 0 を返す
    using CueIsEntityValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);
    /// @brief Transform が存在するなら 1、存在しないなら 0 を返す
    using CueHasTransformFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueEntityHandle a_entityHandle);

    /// @brief Transform の値を取得する
    /// `a_outTransform` は呼び出し側が所有し、確保も解放も呼び出し側が行いる
    using CueGetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    /// @brief Transform の値を設定する
    /// `a_transform` が指すデータは呼び出し側所有で、呼び出し中だけ有効であれば十分
    using CueSetTransformFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Transform の値を度数法の回転で取得する
    /// `position` と `scale` は通常の Transform と同じ単位
    using CueGetTransformDegreesFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformData* a_outTransform
    );

    /// @brief Transform の値を度数法の回転で設定する
    /// `position` と `scale` は通常の Transform と同じ単位
    using CueSetTransformDegreesFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformData* a_transform
    );

    /// @brief Transform の値をクォータニオン回転で取得する
    using CueGetTransformQuaternionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueTransformQuaternionData* a_outTransform
    );

    /// @brief Transform の値をクォータニオン回転で設定する
    using CueSetTransformQuaternionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueTransformQuaternionData* a_transform
    );

    /// @brief 指定キーが押されていれば 1、そうでなければ 0 を返す
    using CuePushKeyFn = uint8_t (CUE_SCRIPT_CALL*)(CueKey a_key);

    using CueGetMouseDeltaFn = CueResult (CUE_SCRIPT_CALL*)(
        CueMouseDeltaData* a_outDelta
    );

    using CuePushMouseButtonFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueMouseButton a_button);

    using CueGetUiButtonStateFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueUiButtonStateData* a_outState
    );

    using CueGetUiCheckboxStateFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueUiCheckboxStateData* a_outState
    );

    using CueSetUiCheckboxCheckedFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        uint8_t a_isChecked
    );

    using CueGetUiSliderStateFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueUiSliderStateData* a_outState
    );

    using CueSetUiSliderValueFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        float a_value
    );

    /// @brief Script DLL が利用可能な Script クラス名を Engine へ通知する
    /// `a_scriptClassName` は UTF-8 の非所有文字列
    using CueRegisterScriptClassFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName
    );

    /// @brief Script クラスに属する public field を Engine へ通知する
    using CueRegisterScriptFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        const CueScriptFieldValue* a_fieldValue
    );

    using CueRegisterScriptFunctionFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        const CueScriptFunctionDefinition* a_functionDefinition
    );

    /// @brief Entity に紐付いた ScriptInstance をクラス名で検索し
    using CueFindScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueStringView a_scriptClassName,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    /// @brief ScriptInstanceHandle が現在も有効なら 1、無効なら 0 を返す
    using CueIsScriptInstanceValidFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueScriptInstanceHandle a_instanceHandle);

    /// @brief Script public field 値を取得する
    using CueGetScriptFieldFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        CueStringView a_fieldName,
        CueScriptFieldValue* a_outFieldValue
    );

    using CueGetScriptObjectFn =
        void* (CUE_SCRIPT_CALL*)(CueScriptInstanceHandle a_instanceHandle);

    /// @brief Script クラスが登録済みなら 1、未登録なら 0 を返す
    using CueIsScriptClassRegisteredFn =
        uint8_t (CUE_SCRIPT_CALL*)(CueStringView a_scriptClassName);

    /// @brief Script クラスに属する public field 定義を取得する
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

    /// @brief Entity の AudioSourceComponent に再生 request を出し
    using CueRequestAudioSourcePlayFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    /// @brief Scene 名から遅延ロードを予約し、予約された SceneId を返す
    using CueRequestSceneLoadFn = CueSceneId (CUE_SCRIPT_CALL*)(
        CueStringView a_sceneName
    );

    /// @brief SceneId の遅延アンロードを予約し
    using CueRequestSceneUnloadFn = CueResult (CUE_SCRIPT_CALL*)(
        CueSceneId a_sceneId
    );

    /// @brief Entity の RigidBodyComponent に線形速度を設定する
    using CueSetRigidBodyLinearVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity
    );

    /// @brief Entity の RigidBodyComponent から線形速度を取得する
    using CueGetRigidBodyLinearVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueFloat3* a_outVelocity
    );

    /// @brief Entity の RigidBodyComponent へ力を加える
    using CueAddRigidBodyForceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_force
    );

    /// @brief Entity の RigidBodyComponent へインパルスを加える
    using CueAddRigidBodyImpulseFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_impulse
    );

    /// @brief Entity の CharacterControllerComponent に水平移動速度を設定する
    using CueSetCharacterMoveVelocityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_velocity
    );

    /// @brief Entity の CharacterControllerComponent にジャンプを要求し
    using CueRequestCharacterJumpFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    /// @brief Entity の NavAgentComponent に目的地を設定する
    using CueSetNavAgentDestinationFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat3* a_destination
    );

    /// @brief Entity の NavAgentComponent に追跡対象 Entity を設定する
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

    using CueSpawnObjectFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueSpawnObjectDesc* a_desc,
        CueEntityHandle* a_outEntityHandle
    );

    using CueInstantiateEntityFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueInstantiateEntityDesc* a_desc,
        CueEntityHandle* a_outEntityHandle
    );

    using CueFindEntitiesByTagFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_tag,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount
    );

    using CueFindEntitiesByNameFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_name,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount
    );

    using CueTriggerOverlapsFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_triggerEntity,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount
    );

    using CueSphereOverlapFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueSphereOverlapDesc* a_desc,
        CueEntityHandle* a_outEntityHandles,
        uint32_t a_capacity,
        uint32_t* a_outCount
    );

    using CueDestroyEntityFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    using CueGetCameraFovYFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        float* a_outFovY
    );

    using CueSetCameraFovYFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        float a_fovY
    );

    using CueAddOrSetComponentFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueComponentKind a_componentKind,
        const void* a_componentData,
        uint32_t a_componentDataSize
    );

    using CueGetParentFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueEntityHandle* a_outParentEntity
    );

    using CueSetParentFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueEntityHandle a_parentEntity,
        uint8_t a_keepsWorldTransform
    );

    using CueDetachParentFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        uint8_t a_keepsWorldTransform
    );

    using CueSetMaterialPropertyBlockFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueMaterialPropertyBlockData* a_propertyBlock
    );

    using CueGetMaterialPropertyBlockFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        CueMaterialPropertyBlockData* a_outPropertyBlock
    );

    using CueClearMaterialPropertyBlockFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle
    );

    using CueSetMaterialColorFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        const CueFloat4* a_color
    );

    using CueSetMaterialShininessFn = CueResult (CUE_SCRIPT_CALL*)(
        CueEntityHandle a_entityHandle,
        float a_shininess
    );

    using CueLoadJsonConfigFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_assetPath,
        CueJsonConfigHandle* a_outConfigHandle
    );

    using CueUnloadJsonConfigFn = CueResult (CUE_SCRIPT_CALL*)(
        CueJsonConfigHandle a_configHandle
    );

    using CueGetJsonConfigBoolFn = CueResult (CUE_SCRIPT_CALL*)(
        CueJsonConfigHandle a_configHandle,
        CueStringView a_keyPath,
        uint8_t* a_outValue
    );

    using CueGetJsonConfigIntFn = CueResult (CUE_SCRIPT_CALL*)(
        CueJsonConfigHandle a_configHandle,
        CueStringView a_keyPath,
        int32_t* a_outValue
    );

    using CueGetJsonConfigFloatFn = CueResult (CUE_SCRIPT_CALL*)(
        CueJsonConfigHandle a_configHandle,
        CueStringView a_keyPath,
        float* a_outValue
    );

    using CueGetJsonConfigStringFn = CueResult (CUE_SCRIPT_CALL*)(
        CueJsonConfigHandle a_configHandle,
        CueStringView a_keyPath,
        char* a_outBuffer,
        uint32_t a_bufferSize,
        uint32_t* a_outRequiredSize
    );

    /// @brief Engine から Script へ渡す関数テーブル
    /// 末尾拡張のみを許可する
    struct CueEngineApi
    {
        /// 呼び出し側がコンパイル時に見えている `CueEngineApi` のサイズ
        uint32_t structSize;
        /// 利用する ABI version 現在は `k_cueScriptAbiVersion` を使いる
        CueScriptAbiVersion abiVersion;
        CueLogFn log;
        CueIsEntityValidFn isEntityValid;
        CueHasTransformFn hasTransform;
        CueGetTransformFn getTransform;
        CueSetTransformFn setTransform;
        /// v7 拡張`structSize` がこのメンバに届く場合だけ参照する
        CuePushKeyFn pushKey;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRegisterScriptClassFn registerScriptClass;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRegisterScriptFieldFn registerScriptField;
        /// v4 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRegisterScriptFunctionFn registerScriptFunction;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueFindScriptInstanceFn findScriptInstance;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueIsScriptInstanceValidFn isScriptInstanceValid;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptFieldFn getScriptField;
        /// v6 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptObjectFn getScriptObject;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueIsScriptClassRegisteredFn isScriptClassRegistered;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptClassFieldFn getScriptClassField;
        /// v4 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueHasScriptClassFunctionFn hasScriptClassFunction;
        /// v5 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueInvokeScriptFunctionFn invokeScriptFunction;
        /// v8 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRequestAudioSourcePlayFn requestAudioSourcePlay;
        /// v9 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRequestSceneLoadFn requestSceneLoad;
        /// v9 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRequestSceneUnloadFn requestSceneUnload;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetRigidBodyLinearVelocityFn setRigidBodyLinearVelocity;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetRigidBodyLinearVelocityFn getRigidBodyLinearVelocity;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueAddRigidBodyForceFn addRigidBodyForce;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueAddRigidBodyImpulseFn addRigidBodyImpulse;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetCharacterMoveVelocityFn setCharacterMoveVelocity;
        /// v10 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRequestCharacterJumpFn requestCharacterJump;
        /// v11 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetNavAgentDestinationFn setNavAgentDestination;
        /// v12 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetNavAgentTargetFn setNavAgentTarget;
        /// v13 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetMouseDeltaFn getMouseDelta;
        /// v13 拡張`structSize` がこのメンバに届く場合だけ参照する
        CuePushMouseButtonFn pushMouseButton;
        /// v14 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRaycastFn raycast;
        /// v15 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDebugDrawLineFn debugDrawLine;
        /// v15 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDebugDrawSphereFn debugDrawSphere;
        /// v15 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDebugDrawBoxFn debugDrawBox;
        /// v16 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetTransformDegreesFn getTransformDegrees;
        /// v16 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetTransformDegreesFn setTransformDegrees;
        /// v17 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetTransformQuaternionFn getTransformQuaternion;
        /// v17 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetTransformQuaternionFn setTransformQuaternion;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSpawnObjectFn spawnObject;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueInstantiateEntityFn instantiateEntity;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueFindEntitiesByTagFn findEntitiesByTag;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueFindEntitiesByNameFn findEntitiesByName;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueTriggerOverlapsFn triggerOverlaps;
        /// v18 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSphereOverlapFn sphereOverlap;
        /// v20 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDestroyEntityFn destroyEntity;
        /// v20 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetCameraFovYFn getCameraFovY;
        /// v20 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetCameraFovYFn setCameraFovY;
        /// v20 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueAddOrSetComponentFn addOrSetComponent;
        /// v21 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetParentFn getParent;
        /// v21 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetParentFn setParent;
        /// v21 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDetachParentFn detachParent;
        /// v22 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetMaterialPropertyBlockFn setMaterialPropertyBlock;
        /// v22 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetMaterialPropertyBlockFn getMaterialPropertyBlock;
        /// v22 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueClearMaterialPropertyBlockFn clearMaterialPropertyBlock;
        /// v22 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetMaterialColorFn setMaterialColor;
        /// v22 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetMaterialShininessFn setMaterialShininess;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueLoadJsonConfigFn loadJsonConfig;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueUnloadJsonConfigFn unloadJsonConfig;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetJsonConfigBoolFn getJsonConfigBool;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetJsonConfigIntFn getJsonConfigInt;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetJsonConfigFloatFn getJsonConfigFloat;
        /// v25 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetJsonConfigStringFn getJsonConfigString;
        /// v26 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetUiButtonStateFn getUiButtonState;
        /// v26 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetUiCheckboxStateFn getUiCheckboxState;
        /// v26 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetUiCheckboxCheckedFn setUiCheckboxChecked;
        /// v26 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetUiSliderStateFn getUiSliderState;
        /// v26 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSetUiSliderValueFn setUiSliderValue;
    };

    /// @brief Script インスタンス生成時の入力
    struct CueScriptCreateInfo
    {
        /// Script が紐付く Entity の外部公開ハンドル
        CueEntityHandle entityHandle;
        /// 生成対象 Script クラス名UTF-8、非所有
        CueStringView scriptName;
        /// Script public field 値配列null の場合は field なしとして扱う
        const CueScriptFieldValue* fieldValues;
        /// `fieldValues` の要素数
        uint32_t fieldValueCount;
    };

    /// @brief Script DLL 側で利用可能な Script 種別を登録し
    /// v1 では必須ではないが、将来の型レジストリ導入を見据えて残す
    using CueRegisterScriptsFn =
        CueResult (CUE_SCRIPT_CALL*)(const CueEngineApi* a_engineApi);

    /// @brief ScriptInstance を生成する
    /// 生成されたハンドルの実体所有権は Script DLL 側にある
    using CueCreateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueScriptCreateInfo* a_createInfo,
        CueScriptInstanceHandle* a_outInstanceHandle
    );

    /// @brief ScriptInstance を破棄する
    /// 解放責務は生成した Script DLL 側にある
    using CueDestroyScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    /// @brief ScriptInstance を 1 フレーム分更新し
    using CueUpdateScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        float a_deltaTimeSeconds
    );

    using CueDispatchScriptCollisionEventFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        CueScriptCollisionEventType a_eventType,
        CueEntityHandle a_otherEntity
    );

    /// @brief ScriptInstance の state サイズを返す
    using CueGetScriptInstanceStateSizeFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        uint32_t* a_outStateSize
    );

    /// @brief ScriptInstance の state を呼び出し側バッファへ serialize し
    using CueSerializeScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief ScriptInstance の state を復元し
    using CueRestoreScriptInstanceFn = CueResult (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize
    );

    /// @brief ScriptInstance の実体 object pointer を返す
    /// `nullptr` は未解決または無効を表す
    using CueGetScriptInstanceObjectFn = void* (CUE_SCRIPT_CALL*)(
        CueScriptInstanceHandle a_instanceHandle
    );

    /// @brief Script クラスの state 署名を返す
    using CueGetScriptStateDescriptorFn = CueResult (CUE_SCRIPT_CALL*)(
        CueStringView a_scriptClassName,
        CueScriptStateDescriptor* a_outDescriptor
    );

    /// @brief Script DLL が Engine へ返す関数テーブル
    /// v1 では末尾拡張のみを許可する
    struct CueScriptExports
    {
        /// 呼び出し側がコンパイル時に見えている `CueScriptExports` のサイズ
        uint32_t structSize;
        /// 利用する ABI version Engine と一致しない場合はロード失敗とし
        CueScriptAbiVersion abiVersion;
        CueRegisterScriptsFn registerScripts;
        CueCreateScriptInstanceFn createScriptInstance;
        CueDestroyScriptInstanceFn destroyScriptInstance;
        CueUpdateScriptInstanceFn updateScriptInstance;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptInstanceStateSizeFn getScriptInstanceStateSize;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueSerializeScriptInstanceFn serializeScriptInstance;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueRestoreScriptInstanceFn restoreScriptInstance;
        /// v1 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptStateDescriptorFn getScriptStateDescriptor;
        /// v5 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueInvokeScriptFunctionFn invokeScriptFunction;
        /// v6 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueGetScriptInstanceObjectFn getScriptInstanceObject;
        /// v19 拡張`structSize` がこのメンバに届く場合だけ参照する
        CueDispatchScriptCollisionEventFn dispatchScriptCollisionEvent;
    };

    /// @brief Script DLL の ABI version を返す
    /// この値は `k_cueScriptAbiVersion` と一致している必要がある
    CUE_SCRIPT_EXPORT CueScriptAbiVersion CUE_SCRIPT_CALL
        cue_script_get_abi_version(void);

    /// @brief Script DLL の export table を返す
    /// `a_outExports` は呼び出し側が確保し、呼び出し前に null でないことが必須
    CUE_SCRIPT_EXPORT CueResult CUE_SCRIPT_CALL cue_script_get_exports(
        CueScriptExports* a_outExports
    );

#ifdef __cplusplus
}
#endif
