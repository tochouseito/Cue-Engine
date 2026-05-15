#pragma once

#include <Native/ScriptAbi.h>
#include <Native/ScriptModuleRuntime.h>
#include <Script/MarionnetteObject.h>

// === C++ includes ===
#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Marionette
{
    using Cue::Core::Native::ScriptClassDefinition;
    using Cue::Core::Native::ScriptFieldReader;
    using Cue::Core::Native::ScriptFunctionBinding;
    using Cue::Core::Native::hash_script_schema;
    using Cue::Core::Native::invoke_script_state_function_adapter;
    using Cue::Core::Native::make_script_string_view;
    using FunctionFlags = CueScriptFunctionFlags;
    using PropertyFlags = CueScriptFieldFlags;

    using EntityHandle = CueEntityHandle;
    using SceneId = CueSceneId;
    using Transform = CueTransformData;
    using Quaternion = CueQuaternion;
    using TransformQuaternion = CueTransformQuaternionData;
    using MouseDelta = CueMouseDeltaData;
    using RaycastDesc = CueRaycastDesc;
    using RaycastHit = CueRaycastHit;
    using Color = CueFloat4;
    inline constexpr SceneId k_invalidSceneId = k_cueInvalidSceneId;

    enum class Key : uint32_t
    {
        Unknown = CueKey_Unknown,
        Escape = CueKey_Escape,
        Tab = CueKey_Tab,
        CapsLock = CueKey_CapsLock,
        LeftShift = CueKey_LeftShift,
        RightShift = CueKey_RightShift,
        LeftControl = CueKey_LeftControl,
        RightControl = CueKey_RightControl,
        LeftAlt = CueKey_LeftAlt,
        RightAlt = CueKey_RightAlt,
        Space = CueKey_Space,
        Enter = CueKey_Enter,
        Backspace = CueKey_Backspace,
        Insert = CueKey_Insert,
        Delete = CueKey_Delete,
        Home = CueKey_Home,
        End = CueKey_End,
        PageUp = CueKey_PageUp,
        PageDown = CueKey_PageDown,
        Left = CueKey_Left,
        Right = CueKey_Right,
        Up = CueKey_Up,
        Down = CueKey_Down,
        Num0 = CueKey_Num0,
        Num1 = CueKey_Num1,
        Num2 = CueKey_Num2,
        Num3 = CueKey_Num3,
        Num4 = CueKey_Num4,
        Num5 = CueKey_Num5,
        Num6 = CueKey_Num6,
        Num7 = CueKey_Num7,
        Num8 = CueKey_Num8,
        Num9 = CueKey_Num9,
        A = CueKey_A,
        B = CueKey_B,
        C = CueKey_C,
        D = CueKey_D,
        E = CueKey_E,
        F = CueKey_F,
        G = CueKey_G,
        H = CueKey_H,
        I = CueKey_I,
        J = CueKey_J,
        K = CueKey_K,
        L = CueKey_L,
        M = CueKey_M,
        N = CueKey_N,
        O = CueKey_O,
        P = CueKey_P,
        Q = CueKey_Q,
        R = CueKey_R,
        S = CueKey_S,
        T = CueKey_T,
        U = CueKey_U,
        V = CueKey_V,
        W = CueKey_W,
        X = CueKey_X,
        Y = CueKey_Y,
        Z = CueKey_Z,
        F1 = CueKey_F1,
        F2 = CueKey_F2,
        F3 = CueKey_F3,
        F4 = CueKey_F4,
        F5 = CueKey_F5,
        F6 = CueKey_F6,
        F7 = CueKey_F7,
        F8 = CueKey_F8,
        F9 = CueKey_F9,
        F10 = CueKey_F10,
        F11 = CueKey_F11,
        F12 = CueKey_F12,
    };

    enum class MouseButton : uint32_t
    {
        Left = CueMouseButton_Left,
        Right = CueMouseButton_Right,
        Middle = CueMouseButton_Middle,
        X1 = CueMouseButton_X1,
        X2 = CueMouseButton_X2,
    };

    inline constexpr PropertyFlags None = CueScriptFieldFlag_None;
    inline constexpr PropertyFlags EditAnywhere =
        CueScriptFieldFlag_EditAnywhere;
    inline constexpr PropertyFlags Serialize = CueScriptFieldFlag_Serialize;
    inline constexpr PropertyFlags ReadOnly = CueScriptFieldFlag_ReadOnly;
    inline constexpr FunctionFlags FunctionNone = CueScriptFunctionFlag_None;

    template<typename T>
    struct ScriptTypeInfo;

    namespace Detail
    {
        template<typename T>
        [[nodiscard]] constexpr std::string_view class_name() noexcept;
    }

    template<typename T>
    class ScriptClassRef;

    template<typename T>
    class ScriptRef;

    class AnyScriptClassRef;
    class AnyScriptRef;

    template<typename T>
    [[nodiscard]] T* Cast(Cue::MarionnetteObject* a_object) noexcept
    {
        return Cue::cue_cast<T>(a_object);
    }

    template<typename T>
    [[nodiscard]] const T* Cast(const Cue::MarionnetteObject* a_object) noexcept
    {
        return Cue::cue_cast<T>(a_object);
    }

    // *** Cue Engine の script は owner Entity にぶら下がる component 的な振る舞いです。
    // *** UE の ActorComponent に近い使い方を正式系とし、Behaviour<T> は
    // *** その owner Marionnette を get_owner()/owner()/self() で参照します。

    template<typename... TFieldValues>
    [[nodiscard]] constexpr auto make_fields(
        TFieldValues... a_fieldValues) noexcept
    {
        return std::array<CueScriptFieldValue, sizeof...(TFieldValues)>{
            a_fieldValues...
        };
    }

    template<typename... TFunctionDefinitions>
    [[nodiscard]] constexpr auto make_functions(
        TFunctionDefinitions... a_functionDefinitions) noexcept
    {
        return std::array<ScriptFunctionBinding, sizeof...(TFunctionDefinitions)>{
            a_functionDefinitions...
        };
    }

    template<typename T, auto TFunction>
    [[nodiscard]] constexpr ScriptFunctionBinding bind_function(
        std::string_view a_name,
        FunctionFlags a_flags = FunctionNone) noexcept
    {
        return ScriptFunctionBinding{
            CueScriptFunctionDefinition{
                CueStringView{
                    a_name.data(),
                    static_cast<uint32_t>(a_name.size())
                },
                a_flags
            },
            &invoke_script_state_function_adapter<T, TFunction>
        };
    }

    template<size_t TNameLength, size_t TGroupLength>
    [[nodiscard]] constexpr CueScriptFieldValue make_script_ref_entity_field(
        const char (&a_name)[TNameLength],
        const char (&a_groupName)[TGroupLength]) noexcept
    {
        return CueScriptFieldValue{
            make_script_string_view(a_name, static_cast<uint32_t>(TNameLength - 1u)),
            CueScriptFieldType_EntityRef,
            0.0f,
            0,
            0,
            0,
            0,
            0,
            CueEntityHandle{ k_cueInvalidHandleValue },
            CueStringView{ nullptr, 0 },
            make_script_string_view(
                a_groupName,
                static_cast<uint32_t>(TGroupLength - 1u)),
            CueScriptFieldReferenceRole_ScriptReferenceEntity,
            static_cast<CueScriptFieldFlags>(
                CueScriptFieldFlag_EditAnywhere |
                CueScriptFieldFlag_Serialize)
        };
    }

    template<typename T, size_t TNameLength, size_t TGroupLength>
    [[nodiscard]] constexpr CueScriptFieldValue make_script_ref_class_field(
        const char (&a_name)[TNameLength],
        const char (&a_groupName)[TGroupLength]) noexcept
    {
        constexpr std::string_view className = Detail::class_name<T>();
        return CueScriptFieldValue{
            make_script_string_view(a_name, static_cast<uint32_t>(TNameLength - 1u)),
            CueScriptFieldType_ClassRef,
            0.0f,
            0,
            0,
            0,
            0,
            0,
            CueEntityHandle{ k_cueInvalidHandleValue },
            CueStringView{
                className.data(),
                static_cast<uint32_t>(className.size())
            },
            make_script_string_view(
                a_groupName,
                static_cast<uint32_t>(TGroupLength - 1u)),
            CueScriptFieldReferenceRole_ScriptReferenceClass,
            static_cast<CueScriptFieldFlags>(
                CueScriptFieldFlag_EditAnywhere |
                CueScriptFieldFlag_Serialize)
        };
    }

    namespace Detail
    {
        inline thread_local const CueEngineApi* g_currentEngineApi = nullptr;

        class ScopedEngineApiContext final
        {
        public:
            explicit ScopedEngineApiContext(
                const CueEngineApi* a_engineApi) noexcept
                : m_previousEngineApi(g_currentEngineApi)
            {
                g_currentEngineApi = a_engineApi;
            }

            ~ScopedEngineApiContext()
            {
                g_currentEngineApi = m_previousEngineApi;
            }

            ScopedEngineApiContext(const ScopedEngineApiContext&) = delete;
            ScopedEngineApiContext& operator=(const ScopedEngineApiContext&) = delete;

        private:
            const CueEngineApi* m_previousEngineApi = nullptr;
        };

        [[nodiscard]] inline const CueEngineApi* current_engine_api() noexcept
        {
            return g_currentEngineApi;
        }

        [[nodiscard]] constexpr float degrees_to_radians(
            float a_degrees) noexcept
        {
            return a_degrees * 0.017453292519943295769f;
        }

        [[nodiscard]] constexpr float radians_to_degrees(
            float a_radians) noexcept
        {
            return a_radians * 57.295779513082320877f;
        }

        [[nodiscard]] constexpr CueFloat3 degrees_to_radians(
            CueFloat3 a_degrees) noexcept
        {
            return CueFloat3{
                degrees_to_radians(a_degrees.x),
                degrees_to_radians(a_degrees.y),
                degrees_to_radians(a_degrees.z)
            };
        }

        [[nodiscard]] constexpr CueFloat3 radians_to_degrees(
            CueFloat3 a_radians) noexcept
        {
            return CueFloat3{
                radians_to_degrees(a_radians.x),
                radians_to_degrees(a_radians.y),
                radians_to_degrees(a_radians.z)
            };
        }

        template<typename T>
        struct EmptySavedState final
        {
        };

        template<typename T>
        struct SavedStateSelector final
        {
            using Type = EmptySavedState<T>;
        };

        template<typename T>
            requires requires
            {
                typename T::SavedState;
            }
        struct SavedStateSelector<T> final
        {
            using Type = typename T::SavedState;
        };

        template<typename T>
        using saved_state_t = typename SavedStateSelector<T>::Type;

        template<typename>
        inline constexpr bool k_alwaysFalse = false;

        [[nodiscard]] constexpr CueStringView to_cue_string_view(
            std::string_view a_value) noexcept
        {
            return CueStringView{
                a_value.data(),
                static_cast<uint32_t>(a_value.size())
            };
        }

        template<typename T>
        [[nodiscard]] constexpr std::string_view class_name() noexcept
        {
            return ScriptTypeInfo<T>::k_className;
        }

        template<typename T>
        [[nodiscard]] constexpr uint32_t state_version() noexcept
        {
            if constexpr (requires
                {
                    T::k_stateVersion;
                })
            {
                return static_cast<uint32_t>(T::k_stateVersion);
            }

            return 1u;
        }

        template<typename T>
        [[nodiscard]] constexpr std::string_view state_schema() noexcept
        {
            if constexpr (requires
                {
                    T::k_stateSchema;
                })
            {
                return T::k_stateSchema;
            }

            return class_name<T>();
        }

        template<typename T>
        [[nodiscard]] std::span<const CueScriptFieldValue> fields() noexcept
        {
            if constexpr (requires
                {
                    { T::fields() } -> std::convertible_to<std::span<const CueScriptFieldValue>>;
                })
            {
                return T::fields();
            }
            else
            {
                static constexpr auto k_emptyFields = make_fields();
                return std::span<const CueScriptFieldValue>(
                    k_emptyFields.data(),
                    k_emptyFields.size());
            }
        }

        template<typename T>
        [[nodiscard]] std::span<const ScriptFunctionBinding> functions() noexcept
        {
            if constexpr (requires
                {
                    { T::functions() } -> std::convertible_to<std::span<const ScriptFunctionBinding>>;
                })
            {
                return T::functions();
            }
            else
            {
                static constexpr auto k_emptyFunctions = make_functions();
                return std::span<const ScriptFunctionBinding>(
                    k_emptyFunctions.data(),
                    k_emptyFunctions.size());
            }
        }

        template<typename T>
        [[nodiscard]] CueResult bind_fields(
            T& a_behaviour,
            const ScriptFieldReader& a_reader)
        {
            if constexpr (requires
                {
                    { a_behaviour.bind_fields(a_reader) } -> std::same_as<CueResult>;
                })
            {
                return a_behaviour.bind_fields(a_reader);
            }
            else if constexpr (requires
                {
                    { a_behaviour.bind_fields(a_reader) } -> std::same_as<void>;
                })
            {
                a_behaviour.bind_fields(a_reader);
                return CueResult_Ok;
            }
            else
            {
                return CueResult_Ok;
            }
        }

        template<typename T>
        [[nodiscard]] CueResult invoke_start(T& a_behaviour)
        {
            if constexpr (requires
                {
                    { a_behaviour.start() } -> std::same_as<CueResult>;
                })
            {
                return a_behaviour.start();
            }
            else if constexpr (requires
                {
                    { a_behaviour.start() } -> std::same_as<void>;
                })
            {
                a_behaviour.start();
                return CueResult_Ok;
            }
            else
            {
                return CueResult_Ok;
            }
        }

        template<typename T>
        [[nodiscard]] CueResult invoke_update(
            T& a_behaviour,
            float a_deltaTimeSeconds)
        {
            if constexpr (requires
                {
                    { a_behaviour.update() } -> std::same_as<CueResult>;
                })
            {
                (void)a_deltaTimeSeconds;
                return a_behaviour.update();
            }
            else if constexpr (requires
                {
                    { a_behaviour.update() } -> std::same_as<void>;
                })
            {
                (void)a_deltaTimeSeconds;
                a_behaviour.update();
                return CueResult_Ok;
            }
            else if constexpr (requires
                {
                    { a_behaviour.update(a_deltaTimeSeconds) } -> std::same_as<CueResult>;
                })
            {
                return a_behaviour.update(a_deltaTimeSeconds);
            }
            else if constexpr (requires
                {
                    { a_behaviour.update(a_deltaTimeSeconds) } -> std::same_as<void>;
                })
            {
                a_behaviour.update(a_deltaTimeSeconds);
                return CueResult_Ok;
            }
            else
            {
                return CueResult_Ok;
            }
        }

        template<typename T>
        [[nodiscard]] CueResult save_state(
            const T& a_behaviour,
            saved_state_t<T>& a_outState)
        {
            if constexpr (requires
                {
                    typename T::SavedState;
                })
            {
                if constexpr (requires
                    {
                        { a_behaviour.save_state(a_outState) } -> std::same_as<CueResult>;
                    })
                {
                    return a_behaviour.save_state(a_outState);
                }
                else if constexpr (requires
                    {
                        { a_behaviour.save_state(a_outState) } -> std::same_as<void>;
                    })
                {
                    a_behaviour.save_state(a_outState);
                    return CueResult_Ok;
                }
                else
                {
                    static_assert(k_alwaysFalse<T>,
                        "Marionette script with SavedState must implement save_state(SavedState&).");
                }
            }
            else
            {
                (void)a_behaviour;
                (void)a_outState;
                return CueResult_Ok;
            }
        }

        template<typename T>
        [[nodiscard]] CueResult restore_state(
            T& a_behaviour,
            const saved_state_t<T>& a_state)
        {
            if constexpr (requires
                {
                    typename T::SavedState;
                })
            {
                if constexpr (requires
                    {
                        { a_behaviour.restore_state(a_state) } -> std::same_as<CueResult>;
                    })
                {
                    return a_behaviour.restore_state(a_state);
                }
                else if constexpr (requires
                    {
                        { a_behaviour.restore_state(a_state) } -> std::same_as<void>;
                    })
                {
                    a_behaviour.restore_state(a_state);
                    return CueResult_Ok;
                }
                else
                {
                    static_assert(k_alwaysFalse<T>,
                        "Marionette script with SavedState must implement restore_state(const SavedState&).");
                }
            }
            else
            {
                (void)a_behaviour;
                (void)a_state;
                return CueResult_Ok;
            }
        }
    }

    template<typename T>
    struct StateBlob final
    {
        uint32_t version = Detail::state_version<T>();
        CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
        uint8_t hasStarted = 0;
        uint8_t reserved0 = 0;
        uint8_t reserved1 = 0;
        uint8_t reserved2 = 0;
        uint8_t reserved3 = 0;
        uint8_t reserved4 = 0;
        uint8_t reserved5 = 0;
        uint8_t reserved6 = 0;
        typename Detail::saved_state_t<T> savedState{};
    };

    class SceneManagerRef final
    {
    public:
        [[nodiscard]] SceneId load_scene(std::string_view a_sceneName) const noexcept
        {
            const CueEngineApi* engineApi = Detail::current_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, requestSceneLoad) +
                        sizeof(CueRequestSceneLoadFn) ||
                engineApi->requestSceneLoad == nullptr)
            {
                return k_invalidSceneId;
            }

            return engineApi->requestSceneLoad(
                Detail::to_cue_string_view(a_sceneName));
        }

        [[nodiscard]] CueResult unload_scene(SceneId a_sceneId) const noexcept
        {
            const CueEngineApi* engineApi = Detail::current_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, requestSceneUnload) +
                        sizeof(CueRequestSceneUnloadFn) ||
                engineApi->requestSceneUnload == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->requestSceneUnload(a_sceneId);
        }
    };

    inline SceneManagerRef SceneManager{};

    class AnyScriptRef final
    {
    public:
        AnyScriptRef() = default;

        AnyScriptRef(
            const CueEngineApi* a_engineApi,
            CueScriptInstanceHandle a_instanceHandle,
            std::string_view a_className = {}) noexcept
            : m_engineApi(a_engineApi)
            , m_instanceHandle(a_instanceHandle)
            , m_className(a_className)
        {
        }

        [[nodiscard]] CueScriptInstanceHandle instance_handle() const noexcept
        {
            return m_instanceHandle;
        }

        [[nodiscard]] const CueEngineApi* engine_api() const noexcept
        {
            return m_engineApi;
        }

        [[nodiscard]] std::string_view class_name() const noexcept
        {
            return m_className;
        }

        [[nodiscard]] Cue::MarionnetteObject* object() const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->structSize <
                    offsetof(CueEngineApi, getScriptObject) +
                        sizeof(CueGetScriptObjectFn) ||
                m_engineApi->getScriptObject == nullptr ||
                m_instanceHandle.value == k_cueInvalidHandleValue)
            {
                return nullptr;
            }

            return static_cast<Cue::MarionnetteObject*>(
                m_engineApi->getScriptObject(m_instanceHandle));
        }

        [[nodiscard]] AnyScriptClassRef script_class() const noexcept;

        [[nodiscard]] CueResult invoke(std::string_view a_functionName) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->invokeScriptFunction == nullptr ||
                m_instanceHandle.value == k_cueInvalidHandleValue ||
                a_functionName.empty())
            {
                return CueResult_InvalidArgument;
            }

            return m_engineApi->invokeScriptFunction(
                m_instanceHandle,
                Detail::to_cue_string_view(a_functionName));
        }

        template<typename T>
        [[nodiscard]] ScriptRef<T> cast() const noexcept;

        template<typename T>
        [[nodiscard]] bool is_a() const noexcept
        {
            return is_valid() && m_className == Detail::class_name<T>();
        }

        [[nodiscard]] bool is_valid() const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->isScriptInstanceValid != nullptr &&
                m_instanceHandle.value != k_cueInvalidHandleValue &&
                m_engineApi->isScriptInstanceValid(m_instanceHandle) != 0;
        }

        [[nodiscard]] bool get_field(
            std::string_view a_fieldName,
            CueScriptFieldValue& a_outFieldValue) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->getScriptField == nullptr ||
                a_fieldName.empty())
            {
                return false;
            }

            return m_engineApi->getScriptField(
                       m_instanceHandle,
                       Detail::to_cue_string_view(a_fieldName),
                       &a_outFieldValue) == CueResult_Ok;
        }

        [[nodiscard]] bool get_float(
            std::string_view a_fieldName,
            float& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Float)
            {
                return false;
            }

            a_outValue = fieldValue.floatValue;
            return true;
        }

        [[nodiscard]] bool get_int32(
            std::string_view a_fieldName,
            int32_t& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Int32)
            {
                return false;
            }

            a_outValue = fieldValue.intValue;
            return true;
        }

        [[nodiscard]] bool get_bool(
            std::string_view a_fieldName,
            bool& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Bool)
            {
                return false;
            }

            a_outValue = fieldValue.boolValue != 0;
            return true;
        }

        [[nodiscard]] bool get_entity_handle(
            std::string_view a_fieldName,
            CueEntityHandle& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_EntityRef)
            {
                return false;
            }

            a_outValue = fieldValue.entityValue;
            return true;
        }

        [[nodiscard]] bool get_script_class_name(
            std::string_view a_fieldName,
            std::string& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_ClassRef)
            {
                return false;
            }

            a_outValue.assign(
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.data
                    : "",
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.size
                    : 0u);
            return true;
        }

    private:
        const CueEngineApi* m_engineApi = nullptr;
        CueScriptInstanceHandle m_instanceHandle{ k_cueInvalidHandleValue };
        std::string m_className{};
    };

    template<typename T>
    class ScriptRef final
    {
    public:
        ScriptRef() = default;

        ScriptRef(
            const CueEngineApi* a_engineApi,
            CueScriptInstanceHandle a_instanceHandle) noexcept
            : m_engineApi(a_engineApi)
            , m_instanceHandle(a_instanceHandle)
        {
        }

        [[nodiscard]] CueScriptInstanceHandle instance_handle() const noexcept
        {
            return m_instanceHandle;
        }

        [[nodiscard]] const CueEngineApi* engine_api() const noexcept
        {
            return m_engineApi;
        }

        [[nodiscard]] ScriptClassRef<T> script_class() const noexcept
        {
            return ScriptClassRef<T>(
                m_engineApi,
                Detail::class_name<T>());
        }

        [[nodiscard]] AnyScriptRef to_any() const noexcept
        {
            return AnyScriptRef(
                m_engineApi,
                m_instanceHandle,
                Detail::class_name<T>());
        }

        [[nodiscard]] T* get() const noexcept
        {
            return Cast<T>(to_any().object());
        }

        [[nodiscard]] T* operator->() const noexcept
        {
            return get();
        }

        [[nodiscard]] bool is_valid() const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->isScriptInstanceValid != nullptr &&
                m_instanceHandle.value != k_cueInvalidHandleValue &&
                m_engineApi->isScriptInstanceValid(m_instanceHandle) != 0;
        }

        [[nodiscard]] CueResult invoke(std::string_view a_functionName) const noexcept
        {
            return to_any().invoke(a_functionName);
        }

        template<typename TOtherScript>
        [[nodiscard]] bool is_a() const noexcept
        {
            return is_valid() &&
                Detail::class_name<T>() == Detail::class_name<TOtherScript>();
        }

        [[nodiscard]] bool get_field(
            std::string_view a_fieldName,
            CueScriptFieldValue& a_outFieldValue) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->getScriptField == nullptr ||
                a_fieldName.empty())
            {
                return false;
            }

            return m_engineApi->getScriptField(
                       m_instanceHandle,
                       Detail::to_cue_string_view(a_fieldName),
                       &a_outFieldValue) == CueResult_Ok;
        }

        [[nodiscard]] bool get_float(
            std::string_view a_fieldName,
            float& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Float)
            {
                return false;
            }

            a_outValue = fieldValue.floatValue;
            return true;
        }

        [[nodiscard]] bool get_int32(
            std::string_view a_fieldName,
            int32_t& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Int32)
            {
                return false;
            }

            a_outValue = fieldValue.intValue;
            return true;
        }

        [[nodiscard]] bool get_bool(
            std::string_view a_fieldName,
            bool& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Bool)
            {
                return false;
            }

            a_outValue = fieldValue.boolValue != 0;
            return true;
        }

        [[nodiscard]] bool get_entity_handle(
            std::string_view a_fieldName,
            CueEntityHandle& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_EntityRef)
            {
                return false;
            }

            a_outValue = fieldValue.entityValue;
            return true;
        }

        [[nodiscard]] bool get_script_class_name(
            std::string_view a_fieldName,
            std::string& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_ClassRef)
            {
                return false;
            }

            a_outValue.assign(
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.data
                    : "",
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.size
                    : 0u);
            return true;
        }

    private:
        const CueEngineApi* m_engineApi = nullptr;
        CueScriptInstanceHandle m_instanceHandle{ k_cueInvalidHandleValue };
        std::string m_className{};
    };

    template<typename T>
    class ScriptObjectPtr final
    {
    public:
        ScriptObjectPtr() = default;

        ScriptObjectPtr(
            CueEntityHandle a_entityHandle,
            std::string a_className = std::string(Detail::class_name<T>())) noexcept
            : m_entityHandle(a_entityHandle)
            , m_className(std::move(a_className))
        {
        }

        [[nodiscard]] CueEntityHandle entity_handle() const noexcept
        {
            return m_entityHandle;
        }

        [[nodiscard]] std::string_view class_name() const noexcept
        {
            if (!m_className.empty())
            {
                return m_className;
            }

            return Detail::class_name<T>();
        }

        void set_entity_handle(CueEntityHandle a_entityHandle) noexcept
        {
            m_entityHandle = a_entityHandle;
        }

        void set_class_name(std::string_view a_className)
        {
            m_className.assign(a_className);
        }

        void reset() noexcept
        {
            m_entityHandle = CueEntityHandle{ k_cueInvalidHandleValue };
            m_className.clear();
        }

        [[nodiscard]] bool is_set() const noexcept
        {
            return m_entityHandle.value != k_cueInvalidHandleValue;
        }

        [[nodiscard]] bool is_valid() const noexcept
        {
            return script_ref().is_valid();
        }

        [[nodiscard]] ScriptRef<T> script_ref() const noexcept
        {
            const CueEngineApi* engineApi = Detail::current_engine_api();
            if (engineApi == nullptr ||
                engineApi->findScriptInstance == nullptr ||
                m_entityHandle.value == k_cueInvalidHandleValue)
            {
                return {};
            }

            const std::string_view className = this->class_name();
            if (className.empty())
            {
                return {};
            }

            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            if (engineApi->findScriptInstance(
                    m_entityHandle,
                    Detail::to_cue_string_view(className),
                    &instanceHandle) != CueResult_Ok)
            {
                return {};
            }

            return ScriptRef<T>(engineApi, instanceHandle);
        }

        [[nodiscard]] T* get() const noexcept
        {
            return script_ref().get();
        }

        [[nodiscard]] T* operator->() const noexcept
        {
            return get();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return is_valid();
        }

    private:
        CueEntityHandle m_entityHandle{ k_cueInvalidHandleValue };
        std::string m_className{};
    };

    class AnyScriptClassRef final
    {
    public:
        AnyScriptClassRef() = default;

        AnyScriptClassRef(
            const CueEngineApi* a_engineApi,
            std::string_view a_className) noexcept
            : m_engineApi(a_engineApi)
            , m_className(a_className)
        {
        }

        [[nodiscard]] std::string_view class_name() const noexcept
        {
            return m_className;
        }

        [[nodiscard]] const CueEngineApi* engine_api() const noexcept
        {
            return m_engineApi;
        }

        [[nodiscard]] bool is_registered() const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->isScriptClassRegistered != nullptr &&
                !m_className.empty() &&
                m_engineApi->isScriptClassRegistered(
                    Detail::to_cue_string_view(m_className)) != 0;
        }

        template<typename T>
        [[nodiscard]] bool is_a() const noexcept
        {
            return m_className == Detail::class_name<T>();
        }

        template<typename T>
        [[nodiscard]] ScriptClassRef<T> cast() const noexcept;

        [[nodiscard]] bool get_field(
            std::string_view a_fieldName,
            CueScriptFieldValue& a_outFieldValue) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->getScriptClassField == nullptr ||
                m_className.empty() ||
                a_fieldName.empty())
            {
                return false;
            }

            return m_engineApi->getScriptClassField(
                       Detail::to_cue_string_view(m_className),
                       Detail::to_cue_string_view(a_fieldName),
                       &a_outFieldValue) == CueResult_Ok;
        }

        [[nodiscard]] bool has_field(std::string_view a_fieldName) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            return get_field(a_fieldName, fieldValue);
        }

        [[nodiscard]] bool has_function(std::string_view a_functionName) const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->hasScriptClassFunction != nullptr &&
                !m_className.empty() &&
                !a_functionName.empty() &&
                m_engineApi->hasScriptClassFunction(
                    Detail::to_cue_string_view(m_className),
                    Detail::to_cue_string_view(a_functionName)) != 0;
        }

        [[nodiscard]] AnyScriptRef find_instance(
            CueEntityHandle a_entityHandle) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->findScriptInstance == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue ||
                m_className.empty())
            {
                return {};
            }

            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            if (m_engineApi->findScriptInstance(
                    a_entityHandle,
                    Detail::to_cue_string_view(m_className),
                    &instanceHandle) != CueResult_Ok)
            {
                return {};
            }

            return AnyScriptRef(m_engineApi, instanceHandle, m_className);
        }

        [[nodiscard]] bool get_float_default(
            std::string_view a_fieldName,
            float& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Float)
            {
                return false;
            }

            a_outValue = fieldValue.floatValue;
            return true;
        }

        [[nodiscard]] bool get_int32_default(
            std::string_view a_fieldName,
            int32_t& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Int32)
            {
                return false;
            }

            a_outValue = fieldValue.intValue;
            return true;
        }

        [[nodiscard]] bool get_bool_default(
            std::string_view a_fieldName,
            bool& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Bool)
            {
                return false;
            }

            a_outValue = fieldValue.boolValue != 0;
            return true;
        }

        [[nodiscard]] bool get_entity_handle_default(
            std::string_view a_fieldName,
            CueEntityHandle& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_EntityRef)
            {
                return false;
            }

            a_outValue = fieldValue.entityValue;
            return true;
        }

        [[nodiscard]] bool get_script_class_name_default(
            std::string_view a_fieldName,
            std::string& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_ClassRef)
            {
                return false;
            }

            a_outValue.assign(
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.data
                    : "",
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.size
                    : 0u);
            return true;
        }

    private:
        const CueEngineApi* m_engineApi = nullptr;
        std::string m_className{};
    };

    inline AnyScriptClassRef AnyScriptRef::script_class() const noexcept
    {
        return AnyScriptClassRef(m_engineApi, m_className);
    }

    template<typename T>
    inline ScriptClassRef<T> AnyScriptClassRef::cast() const noexcept
    {
        if (m_className != Detail::class_name<T>())
        {
            return {};
        }

        return ScriptClassRef<T>(m_engineApi, m_className);
    }

    template<typename T>
    class ScriptClassRef final
    {
    public:
        ScriptClassRef() = default;

        ScriptClassRef(
            const CueEngineApi* a_engineApi,
            std::string_view a_className) noexcept
            : m_engineApi(a_engineApi)
            , m_className(a_className)
        {
        }

        [[nodiscard]] std::string_view class_name() const noexcept
        {
            return m_className;
        }

        [[nodiscard]] const CueEngineApi* engine_api() const noexcept
        {
            return m_engineApi;
        }

        [[nodiscard]] bool is_registered() const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->isScriptClassRegistered != nullptr &&
                !m_className.empty() &&
                m_engineApi->isScriptClassRegistered(
                    Detail::to_cue_string_view(m_className)) != 0;
        }

        template<typename TOtherScript>
        [[nodiscard]] bool is_a() const noexcept
        {
            return m_className == Detail::class_name<TOtherScript>();
        }

        [[nodiscard]] bool get_field(
            std::string_view a_fieldName,
            CueScriptFieldValue& a_outFieldValue) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->getScriptClassField == nullptr ||
                m_className.empty() ||
                a_fieldName.empty())
            {
                return false;
            }

            return m_engineApi->getScriptClassField(
                       Detail::to_cue_string_view(m_className),
                       Detail::to_cue_string_view(a_fieldName),
                       &a_outFieldValue) == CueResult_Ok;
        }

        [[nodiscard]] bool has_field(std::string_view a_fieldName) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            return get_field(a_fieldName, fieldValue);
        }

        [[nodiscard]] bool has_function(std::string_view a_functionName) const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->hasScriptClassFunction != nullptr &&
                !m_className.empty() &&
                !a_functionName.empty() &&
                m_engineApi->hasScriptClassFunction(
                    Detail::to_cue_string_view(m_className),
                    Detail::to_cue_string_view(a_functionName)) != 0;
        }

        [[nodiscard]] bool get_float_default(
            std::string_view a_fieldName,
            float& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Float)
            {
                return false;
            }

            a_outValue = fieldValue.floatValue;
            return true;
        }

        [[nodiscard]] bool get_int32_default(
            std::string_view a_fieldName,
            int32_t& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Int32)
            {
                return false;
            }

            a_outValue = fieldValue.intValue;
            return true;
        }

        [[nodiscard]] bool get_bool_default(
            std::string_view a_fieldName,
            bool& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_Bool)
            {
                return false;
            }

            a_outValue = fieldValue.boolValue != 0;
            return true;
        }

        [[nodiscard]] bool get_entity_handle_default(
            std::string_view a_fieldName,
            CueEntityHandle& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_EntityRef)
            {
                return false;
            }

            a_outValue = fieldValue.entityValue;
            return true;
        }

        [[nodiscard]] bool get_script_class_name_default(
            std::string_view a_fieldName,
            std::string& a_outValue) const noexcept
        {
            CueScriptFieldValue fieldValue{};
            if (!get_field(a_fieldName, fieldValue) ||
                fieldValue.type != CueScriptFieldType_ClassRef)
            {
                return false;
            }

            a_outValue.assign(
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.data
                    : "",
                fieldValue.classValue.data != nullptr
                    ? fieldValue.classValue.size
                    : 0u);
            return true;
        }

        [[nodiscard]] AnyScriptClassRef to_any() const noexcept
        {
            return AnyScriptClassRef(m_engineApi, m_className);
        }

        [[nodiscard]] ScriptRef<T> find_instance(
            CueEntityHandle a_entityHandle) const noexcept
        {
            return to_any().find_instance(a_entityHandle).template cast<T>();
        }

    private:
        const CueEngineApi* m_engineApi = nullptr;
        std::string_view m_className{};
    };

    template<typename T>
    inline ScriptRef<T> AnyScriptRef::cast() const noexcept
    {
        if (m_className != Detail::class_name<T>())
        {
            return {};
        }

        return ScriptRef<T>(m_engineApi, m_instanceHandle);
    }

    template<typename T>
    [[nodiscard]] inline ScriptRef<T> Cast(const AnyScriptRef& a_scriptRef) noexcept
    {
        return a_scriptRef.template cast<T>();
    }

    template<typename T>
    [[nodiscard]] inline ScriptClassRef<T> Cast(
        const AnyScriptClassRef& a_scriptClass) noexcept
    {
        return a_scriptClass.template cast<T>();
    }

    template<typename T>
    [[nodiscard]] inline ScriptClassRef<T> Cast(
        const ScriptClassRef<T>& a_scriptClass) noexcept
    {
        return a_scriptClass;
    }

    template<typename T>
    class Behaviour : public Cue::MarionnetteObject
    {
    public:
        [[nodiscard]] static const Cue::MarionnetteClass* static_class() noexcept
        {
            static const Cue::MarionnetteClass k_scriptClass{
                Detail::class_name<T>().data(),
                Cue::MarionnetteObject::static_class(),
                nullptr,
                nullptr,
                0,
                nullptr,
                0
            };
            return &k_scriptClass;
        }

        [[nodiscard]] const Cue::MarionnetteClass* get_class() const noexcept override
        {
            return static_class();
        }

        [[nodiscard]] static CueResult create(
            const CueScriptCreateInfo* a_createInfo,
            T& a_state)
        {
            if (a_createInfo == nullptr)
            {
                return CueResult_InvalidArgument;
            }
            if (a_createInfo->entityHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidArgument;
            }

            a_state.m_entityHandle = a_createInfo->entityHandle;
            a_state.m_hasStarted = false;
            a_state.m_deltaTimeSeconds = 0.0f;

            const ScriptFieldReader fieldReader(a_createInfo);
            return Detail::bind_fields(a_state, fieldReader);
        }

        [[nodiscard]] CueResult update(
            const CueEngineApi* a_engineApi,
            float a_deltaTimeSeconds)
        {
            if (a_engineApi == nullptr)
            {
                return CueResult_InvalidState;
            }

            m_engineApi = a_engineApi;
            m_deltaTimeSeconds = a_deltaTimeSeconds;
            const Detail::ScopedEngineApiContext engineApiContext(a_engineApi);

            if (!m_hasStarted)
            {
                const CueResult startResult =
                    Detail::invoke_start(static_cast<T&>(*this));
                if (startResult != CueResult_Ok)
                {
                    m_deltaTimeSeconds = 0.0f;
                    m_engineApi = nullptr;
                    return startResult;
                }

                m_hasStarted = true;
            }

            const CueResult updateResult =
                Detail::invoke_update(
                    static_cast<T&>(*this),
                    a_deltaTimeSeconds);
            m_deltaTimeSeconds = 0.0f;
            m_engineApi = nullptr;
            return updateResult;
        }

        [[nodiscard]] CueResult serialize(StateBlob<T>& a_outState) const
        {
            a_outState.version = Detail::state_version<T>();
            a_outState.entityHandle = m_entityHandle;
            a_outState.hasStarted = m_hasStarted ? 1u : 0u;
            return Detail::save_state(
                static_cast<const T&>(*this),
                a_outState.savedState);
        }

        [[nodiscard]] CueResult restore(const StateBlob<T>& a_state)
        {
            if (a_state.version != Detail::state_version<T>())
            {
                return CueResult_Unsupported;
            }

            m_entityHandle = a_state.entityHandle;
            m_hasStarted = a_state.hasStarted != 0;
            return Detail::restore_state(
                static_cast<T&>(*this),
                a_state.savedState);
        }

        void begin_script_call(const CueEngineApi* a_engineApi) noexcept
        {
            m_engineApi = a_engineApi;
            Detail::g_currentEngineApi = a_engineApi;
        }

        void end_script_call() noexcept
        {
            m_engineApi = nullptr;
            Detail::g_currentEngineApi = nullptr;
        }

    protected:
        using Transform = Marionette::Transform;

        [[nodiscard]] CueEntityHandle entity_handle() const noexcept
        {
            return m_entityHandle;
        }

        [[nodiscard]] CueEntityHandle get_owner() const noexcept
        {
            return m_entityHandle;
        }

        [[nodiscard]] CueEntityHandle owner() const noexcept
        {
            return get_owner();
        }

        [[nodiscard]] CueEntityHandle self() const noexcept
        {
            return get_owner();
        }

        [[nodiscard]] float delta_time() const noexcept
        {
            return m_deltaTimeSeconds;
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_script() const noexcept
        {
            return find_script<TOtherScript>(m_entityHandle);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_component_by_class() const noexcept
        {
            return get_script<TOtherScript>();
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_component() const noexcept
        {
            return get_component_by_class<TOtherScript>();
        }

        [[nodiscard]] AnyScriptRef get_script(
            std::string_view a_scriptClassName) const noexcept
        {
            return find_script(m_entityHandle, a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef get_component_by_class(
            std::string_view a_scriptClassName) const noexcept
        {
            return get_script(a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef get_component(
            std::string_view a_scriptClassName) const noexcept
        {
            return get_component_by_class(a_scriptClassName);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptClassRef<TOtherScript> get_script_class() const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            return ScriptClassRef<TOtherScript>(
                engineApi,
                Detail::class_name<TOtherScript>());
        }

        [[nodiscard]] AnyScriptClassRef make_script_class_ref(
            std::string_view a_scriptClassName) const noexcept
        {
            return AnyScriptClassRef(runtime_engine_api(), a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef get_script(
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return a_scriptClass.find_instance(m_entityHandle);
        }

        [[nodiscard]] AnyScriptRef get_component_by_class(
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return get_script(a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_component_by_class(
            const ScriptClassRef<TOtherScript>& a_scriptClass) const noexcept
        {
            return a_scriptClass.find_instance(m_entityHandle);
        }

        [[nodiscard]] AnyScriptRef get_component(
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return get_component_by_class(a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_component(
            const ScriptClassRef<TOtherScript>& a_scriptClass) const noexcept
        {
            return get_component_by_class(a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> find_script(
            CueEntityHandle a_entityHandle) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->findScriptInstance == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue)
            {
                return {};
            }

            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            if (engineApi->findScriptInstance(
                    a_entityHandle,
                    Detail::to_cue_string_view(
                        Detail::class_name<TOtherScript>()),
                    &instanceHandle) != CueResult_Ok)
            {
                return {};
            }

            return ScriptRef<TOtherScript>(engineApi, instanceHandle);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> find_component_by_class(
            CueEntityHandle a_entityHandle) const noexcept
        {
            return find_script<TOtherScript>(a_entityHandle);
        }

        [[nodiscard]] AnyScriptRef find_script(
            CueEntityHandle a_entityHandle,
            std::string_view a_scriptClassName) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->findScriptInstance == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue ||
                a_scriptClassName.empty())
            {
                return {};
            }

            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            if (engineApi->findScriptInstance(
                    a_entityHandle,
                    Detail::to_cue_string_view(a_scriptClassName),
                    &instanceHandle) != CueResult_Ok)
            {
                return {};
            }

            return AnyScriptRef(engineApi, instanceHandle, a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef find_component_by_class(
            CueEntityHandle a_entityHandle,
            std::string_view a_scriptClassName) const noexcept
        {
            return find_script(a_entityHandle, a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef find_component(
            CueEntityHandle a_entityHandle,
            std::string_view a_scriptClassName) const noexcept
        {
            return find_component_by_class(a_entityHandle, a_scriptClassName);
        }

        [[nodiscard]] AnyScriptRef find_script(
            CueEntityHandle a_entityHandle,
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return a_scriptClass.find_instance(a_entityHandle);
        }

        [[nodiscard]] AnyScriptRef find_component_by_class(
            CueEntityHandle a_entityHandle,
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return find_script(a_entityHandle, a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> find_component_by_class(
            CueEntityHandle a_entityHandle,
            const ScriptClassRef<TOtherScript>& a_scriptClass) const noexcept
        {
            return a_scriptClass.find_instance(a_entityHandle);
        }

        [[nodiscard]] AnyScriptRef find_component(
            CueEntityHandle a_entityHandle,
            const AnyScriptClassRef& a_scriptClass) const noexcept
        {
            return find_component_by_class(a_entityHandle, a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> find_component(
            CueEntityHandle a_entityHandle,
            const ScriptClassRef<TOtherScript>& a_scriptClass) const noexcept
        {
            return find_component_by_class(a_entityHandle, a_scriptClass);
        }

        template<typename TOtherScript>
        [[nodiscard]] bool has_script() const noexcept
        {
            return get_script<TOtherScript>().is_valid();
        }

        template<typename TOtherScript>
        [[nodiscard]] bool has_script(
            CueEntityHandle a_entityHandle) const noexcept
        {
            return find_script<TOtherScript>(a_entityHandle).is_valid();
        }

        [[nodiscard]] bool is_entity_valid() const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            return engineApi != nullptr &&
                engineApi->isEntityValid != nullptr &&
                engineApi->isEntityValid(m_entityHandle) != 0;
        }

        [[nodiscard]] bool has_transform() const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            return engineApi != nullptr &&
                engineApi->hasTransform != nullptr &&
                engineApi->hasTransform(m_entityHandle) != 0;
        }

        [[nodiscard]] CueResult get_transform(
            Transform& a_outTransform) const noexcept
        {
            return get_transform(m_entityHandle, a_outTransform);
        }

        [[nodiscard]] CueResult get_transform(
            CueEntityHandle a_entityHandle,
            Transform& a_outTransform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->getTransform == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidState;
            }

            return engineApi->getTransform(a_entityHandle, &a_outTransform);
        }

        [[nodiscard]] CueResult set_transform(
            const Transform& a_transform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->setTransform == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setTransform(m_entityHandle, &a_transform);
        }

        [[nodiscard]] CueResult get_transform_degrees(
            Transform& a_outTransform) const noexcept
        {
            return get_transform_degrees(m_entityHandle, a_outTransform);
        }

        [[nodiscard]] CueResult get_transform_degrees(
            CueEntityHandle a_entityHandle,
            Transform& a_outTransform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidState;
            }

            if (engineApi->structSize >=
                    offsetof(CueEngineApi, getTransformDegrees) +
                        sizeof(CueGetTransformDegreesFn) &&
                engineApi->getTransformDegrees != nullptr)
            {
                return engineApi->getTransformDegrees(
                    a_entityHandle, &a_outTransform);
            }

            const CueResult result =
                get_transform(a_entityHandle, a_outTransform);
            if (result != CueResult_Ok)
            {
                return result;
            }

            a_outTransform.rotation =
                Detail::radians_to_degrees(a_outTransform.rotation);
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult set_transform_degrees(
            const Transform& a_transform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr)
            {
                return CueResult_InvalidState;
            }

            if (engineApi->structSize >=
                    offsetof(CueEngineApi, setTransformDegrees) +
                        sizeof(CueSetTransformDegreesFn) &&
                engineApi->setTransformDegrees != nullptr)
            {
                return engineApi->setTransformDegrees(
                    m_entityHandle, &a_transform);
            }

            Transform transform = a_transform;
            transform.rotation =
                Detail::degrees_to_radians(transform.rotation);
            return set_transform(transform);
        }

        [[nodiscard]] CueResult get_transform_quaternion(
            TransformQuaternion& a_outTransform) const noexcept
        {
            return get_transform_quaternion(m_entityHandle, a_outTransform);
        }

        [[nodiscard]] CueResult get_transform_quaternion(
            CueEntityHandle a_entityHandle,
            TransformQuaternion& a_outTransform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, getTransformQuaternion) +
                        sizeof(CueGetTransformQuaternionFn) ||
                engineApi->getTransformQuaternion == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidState;
            }

            return engineApi->getTransformQuaternion(
                a_entityHandle, &a_outTransform);
        }

        [[nodiscard]] CueResult set_transform_quaternion(
            const TransformQuaternion& a_transform) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, setTransformQuaternion) +
                        sizeof(CueSetTransformQuaternionFn) ||
                engineApi->setTransformQuaternion == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setTransformQuaternion(
                m_entityHandle, &a_transform);
        }

        [[nodiscard]] CueResult set_rigid_body_linear_velocity(
            const CueFloat3& a_velocity) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, setRigidBodyLinearVelocity) +
                        sizeof(CueSetRigidBodyLinearVelocityFn) ||
                engineApi->setRigidBodyLinearVelocity == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setRigidBodyLinearVelocity(
                m_entityHandle, &a_velocity);
        }

        [[nodiscard]] CueResult get_rigid_body_linear_velocity(
            CueFloat3& a_outVelocity) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, getRigidBodyLinearVelocity) +
                        sizeof(CueGetRigidBodyLinearVelocityFn) ||
                engineApi->getRigidBodyLinearVelocity == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->getRigidBodyLinearVelocity(
                m_entityHandle, &a_outVelocity);
        }

        [[nodiscard]] CueResult add_rigid_body_force(
            const CueFloat3& a_force) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, addRigidBodyForce) +
                        sizeof(CueAddRigidBodyForceFn) ||
                engineApi->addRigidBodyForce == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->addRigidBodyForce(m_entityHandle, &a_force);
        }

        [[nodiscard]] CueResult add_rigid_body_impulse(
            const CueFloat3& a_impulse) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, addRigidBodyImpulse) +
                        sizeof(CueAddRigidBodyImpulseFn) ||
                engineApi->addRigidBodyImpulse == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->addRigidBodyImpulse(m_entityHandle, &a_impulse);
        }

        [[nodiscard]] CueResult set_character_move_velocity(
            const CueFloat3& a_velocity) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, setCharacterMoveVelocity) +
                        sizeof(CueSetCharacterMoveVelocityFn) ||
                engineApi->setCharacterMoveVelocity == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setCharacterMoveVelocity(
                m_entityHandle, &a_velocity);
        }

        [[nodiscard]] CueResult request_character_jump() const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, requestCharacterJump) +
                        sizeof(CueRequestCharacterJumpFn) ||
                engineApi->requestCharacterJump == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->requestCharacterJump(m_entityHandle);
        }

        [[nodiscard]] CueResult set_nav_agent_destination(
            const CueFloat3& a_destination) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, setNavAgentDestination) +
                        sizeof(CueSetNavAgentDestinationFn) ||
                engineApi->setNavAgentDestination == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setNavAgentDestination(
                m_entityHandle, &a_destination);
        }

        [[nodiscard]] CueResult set_nav_agent_target(
            CueEntityHandle a_targetEntityHandle) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, setNavAgentTarget) +
                        sizeof(CueSetNavAgentTargetFn) ||
                engineApi->setNavAgentTarget == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->setNavAgentTarget(
                m_entityHandle, a_targetEntityHandle);
        }

        [[nodiscard]] bool push_key(Key a_key) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, pushKey) + sizeof(CuePushKeyFn) ||
                engineApi->pushKey == nullptr)
            {
                return false;
            }

            return engineApi->pushKey(static_cast<CueKey>(a_key)) != 0;
        }

        [[nodiscard]] MouseDelta mouse_delta() const noexcept
        {
            MouseDelta delta{};
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, getMouseDelta) +
                        sizeof(CueGetMouseDeltaFn) ||
                engineApi->getMouseDelta == nullptr)
            {
                return delta;
            }

            (void)engineApi->getMouseDelta(&delta);
            return delta;
        }

        [[nodiscard]] bool push_mouse_button(MouseButton a_button) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, pushMouseButton) +
                        sizeof(CuePushMouseButtonFn) ||
                engineApi->pushMouseButton == nullptr)
            {
                return false;
            }

            return engineApi->pushMouseButton(
                       static_cast<CueMouseButton>(a_button)) != 0;
        }

        [[nodiscard]] CueResult raycast(
            const RaycastDesc& a_desc,
            RaycastHit& a_outHit) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, raycast) + sizeof(CueRaycastFn) ||
                engineApi->raycast == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->raycast(&a_desc, &a_outHit);
        }

        [[nodiscard]] CueResult debug_draw_line(
            const CueFloat3& a_start,
            const CueFloat3& a_end,
            const Color& a_color,
            float a_durationSeconds = 0.0f) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, debugDrawLine) +
                        sizeof(CueDebugDrawLineFn) ||
                engineApi->debugDrawLine == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->debugDrawLine(
                &a_start, &a_end, &a_color, a_durationSeconds);
        }

        [[nodiscard]] CueResult debug_draw_sphere(
            const CueFloat3& a_center,
            float a_radius,
            const Color& a_color,
            float a_durationSeconds = 0.0f) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, debugDrawSphere) +
                        sizeof(CueDebugDrawSphereFn) ||
                engineApi->debugDrawSphere == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->debugDrawSphere(
                &a_center, a_radius, &a_color, a_durationSeconds);
        }

        [[nodiscard]] CueResult debug_draw_box(
            const CueFloat3& a_center,
            const CueFloat3& a_halfExtent,
            const Color& a_color,
            float a_durationSeconds = 0.0f) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, debugDrawBox) +
                        sizeof(CueDebugDrawBoxFn) ||
                engineApi->debugDrawBox == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->debugDrawBox(
                &a_center, &a_halfExtent, &a_color, a_durationSeconds);
        }

        [[nodiscard]] CueResult request_audio_source_play() const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->structSize <
                    offsetof(CueEngineApi, requestAudioSourcePlay) +
                        sizeof(CueRequestAudioSourcePlayFn) ||
                engineApi->requestAudioSourcePlay == nullptr)
            {
                return CueResult_InvalidState;
            }

            return engineApi->requestAudioSourcePlay(m_entityHandle);
        }

        void log_info(std::string_view a_message) const noexcept
        {
            log(CueLogSeverity_Info, a_message);
        }

        void log_warning(std::string_view a_message) const noexcept
        {
            log(CueLogSeverity_Warning, a_message);
        }

        void log_error(std::string_view a_message) const noexcept
        {
            log(CueLogSeverity_Error, a_message);
        }

        template<size_t N>
        [[nodiscard]] static bool read_float(
            const ScriptFieldReader& a_reader,
            const char (&a_name)[N],
            float& a_outValue) noexcept
        {
            return a_reader.read_float(
                make_script_string_view(
                    a_name,
                    static_cast<uint32_t>(N - 1u)),
                a_outValue);
        }

        template<size_t N>
        [[nodiscard]] static bool read_int32(
            const ScriptFieldReader& a_reader,
            const char (&a_name)[N],
            int32_t& a_outValue) noexcept
        {
            return a_reader.read_int32(
                make_script_string_view(
                    a_name,
                    static_cast<uint32_t>(N - 1u)),
                a_outValue);
        }

        template<size_t N>
        [[nodiscard]] static bool read_bool(
            const ScriptFieldReader& a_reader,
            const char (&a_name)[N],
            bool& a_outValue) noexcept
        {
            return a_reader.read_bool(
                make_script_string_view(
                    a_name,
                    static_cast<uint32_t>(N - 1u)),
                a_outValue);
        }

        template<size_t N>
        [[nodiscard]] static bool read_entity_handle(
            const ScriptFieldReader& a_reader,
            const char (&a_name)[N],
            CueEntityHandle& a_outValue) noexcept
        {
            return a_reader.read_entity_handle(
                make_script_string_view(
                    a_name,
                    static_cast<uint32_t>(N - 1u)),
                a_outValue);
        }

        template<size_t N>
        [[nodiscard]] static bool read_script_class_name(
            const ScriptFieldReader& a_reader,
            const char (&a_name)[N],
            std::string& a_outValue) noexcept
        {
            CueStringView value{};
            if (!a_reader.read_script_class_name(
                    make_script_string_view(
                        a_name,
                        static_cast<uint32_t>(N - 1u)),
                    value))
            {
                return false;
            }

            a_outValue.assign(
                value.data != nullptr ? value.data : "",
                value.data != nullptr ? value.size : 0u);
            return true;
        }

        template<typename TOtherScript, size_t N>
        [[nodiscard]] static bool read_object_ptr(
            const ScriptFieldReader& a_reader,
            const char (&a_baseName)[N],
            ScriptObjectPtr<TOtherScript>& a_outValue) noexcept
        {
            std::string entityFieldName(a_baseName, N - 1u);
            entityFieldName += "Entity";

            std::string classFieldName(a_baseName, N - 1u);
            classFieldName += "ScriptClass";

            CueEntityHandle entityHandle{ k_cueInvalidHandleValue };
            const bool hasEntity = a_reader.read_entity_handle(
                make_script_string_view(
                    entityFieldName.data(),
                    static_cast<uint32_t>(entityFieldName.size())),
                entityHandle);
            CueStringView classNameView{};
            const bool hasClass = a_reader.read_script_class_name(
                make_script_string_view(
                    classFieldName.data(),
                    static_cast<uint32_t>(classFieldName.size())),
                classNameView);

            if (hasEntity)
            {
                a_outValue.set_entity_handle(entityHandle);
            }

            if (hasClass)
            {
                a_outValue.set_class_name(std::string_view(
                    classNameView.data != nullptr ? classNameView.data : "",
                    classNameView.data != nullptr ? classNameView.size : 0u));
            }
            else
            {
                a_outValue.set_class_name(Detail::class_name<TOtherScript>());
            }

            return hasEntity || hasClass;
        }

    private:
        [[nodiscard]] const CueEngineApi* runtime_engine_api() const noexcept
        {
            return m_engineApi != nullptr
                ? m_engineApi
                : Detail::current_engine_api();
        }

        void log(
            CueLogSeverity a_severity,
            std::string_view a_message) const noexcept
        {
            const CueEngineApi* engineApi = runtime_engine_api();
            if (engineApi == nullptr ||
                engineApi->log == nullptr)
            {
                return;
            }

            (void)engineApi->log(
                a_severity,
                make_script_string_view(
                    a_message.data(),
                    static_cast<uint32_t>(a_message.size())));
        }

        const CueEngineApi* m_engineApi = nullptr;
        CueEntityHandle m_entityHandle{ k_cueInvalidHandleValue };
        float m_deltaTimeSeconds = 0.0f;
        bool m_hasStarted = false;
    };

    template<typename T>
    using ComponentBehaviour = Behaviour<T>;

    template<typename T>
    using MarionnetteComponentBehaviour = Behaviour<T>;

    template<typename T>
    [[nodiscard]] ScriptClassDefinition make_script_definition() noexcept
    {
        const std::span<const CueScriptFieldValue> fields =
            Detail::fields<T>();
        const std::span<const ScriptFunctionBinding> functions =
            Detail::functions<T>();

        return ScriptClassDefinition{
            make_script_string_view(
                Detail::class_name<T>().data(),
                static_cast<uint32_t>(Detail::class_name<T>().size())),
            fields.data(),
            static_cast<uint32_t>(fields.size()),
            functions.data(),
            static_cast<uint32_t>(functions.size()),
            CueScriptStateDescriptor{
                Detail::state_version<T>(),
                static_cast<uint32_t>(sizeof(typename T::StateBlob)),
                hash_script_schema(Detail::state_schema<T>())
            },
            &Cue::Core::Native::create_script_state_adapter<T>,
            &Cue::Core::Native::destroy_script_state_adapter<T>,
            &Cue::Core::Native::update_script_state_adapter<T>,
            &Cue::Core::Native::serialize_script_state_adapter<T>,
            &Cue::Core::Native::restore_script_state_adapter<T>
        };
    }
}

#define MARIONETTE_FIELDS(...) \
    [[nodiscard]] static std::span<const CueScriptFieldValue> fields() noexcept \
    { \
        static constexpr auto k_fields = ::Marionette::make_fields(__VA_ARGS__); \
        return std::span<const CueScriptFieldValue>( \
            k_fields.data(), \
            k_fields.size()); \
    }

#define MARIONETTE_NO_FIELDS() \
    [[nodiscard]] static std::span<const CueScriptFieldValue> fields() noexcept \
    { \
        static constexpr auto k_fields = ::Marionette::make_fields(); \
        return std::span<const CueScriptFieldValue>( \
            k_fields.data(), \
            k_fields.size()); \
    }

#define MARIONETTE_FUNCTIONS(...) \
    [[nodiscard]] static std::span<const ::Marionette::ScriptFunctionBinding> functions() noexcept \
    { \
        static constexpr auto k_functions = ::Marionette::make_functions(__VA_ARGS__); \
        return std::span<const ::Marionette::ScriptFunctionBinding>( \
            k_functions.data(), \
            k_functions.size()); \
    }

#define MARIONETTE_NO_FUNCTIONS() \
    [[nodiscard]] static std::span<const ::Marionette::ScriptFunctionBinding> functions() noexcept \
    { \
        static constexpr auto k_functions = ::Marionette::make_functions(); \
        return std::span<const ::Marionette::ScriptFunctionBinding>( \
            k_functions.data(), \
            k_functions.size()); \
    }

#define MARIONETTE_FUNCTION(a_typeName, a_methodName) \
    ::Marionette::bind_function<a_typeName, &a_typeName::a_methodName>( \
        #a_methodName)

#define MARIONETTE_DECLARE_SCRIPT_TYPE(a_typeName, a_classNameLiteral) \
    class a_typeName; \
    template<> \
    struct ::Marionette::ScriptTypeInfo<a_typeName> final \
    { \
        static constexpr std::string_view k_className = a_classNameLiteral; \
    }

#define MARIONETTE_DEFINE_SCRIPT(a_symbolName, a_typeName) \
    Cue::Core::Native::ScriptClassDefinition \
    make_##a_symbolName##_script_definition() noexcept \
    { \
        return ::Marionette::make_script_definition<a_typeName>(); \
    }

#define MARIONETTE_OBJECT_PTR_FIELD(a_typeName, a_baseNameLiteral) \
    ::Marionette::make_script_ref_entity_field( \
        a_baseNameLiteral "Entity", \
        a_baseNameLiteral), \
    ::Marionette::make_script_ref_class_field<a_typeName>( \
        a_baseNameLiteral "ScriptClass", \
        a_baseNameLiteral)
