#pragma once

/// **********************************************************************
/// GameScript の状態と Runtime World を ABI 経由で結び付ける
/// **********************************************************************

// === Native includes ===
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <array>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace Cue::Script
{
    /// @brief GameScript が値として操作するローカル Transform
    using Transform = Core::Native::ScriptTransform;

    template <typename T>
    struct ScriptTypeInfo
    {
        [[nodiscard]] static constexpr std::string_view class_name() noexcept
        {
            return {};
        }
    };

    /// @brief DLL object の所有権を持たず、登録関数だけを呼び出す型付き参照
    template <typename T>
    class ScriptRef final
    {
    public:
        ScriptRef() = default;

        ScriptRef(const Core::Native::ScriptEngineApi* a_engineApi,
                  Core::Native::ScriptInstanceHandle a_instanceHandle) noexcept
            : m_engineApi(a_engineApi), m_instanceHandle(a_instanceHandle)
        {
        }

        [[nodiscard]] bool is_valid() const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->isInstanceValid == nullptr ||
                m_instanceHandle.value ==
                    Core::Native::k_invalidScriptInstanceHandle.value)
            {
                return false;
            }

            uint8_t isValid = 0u;
            return m_engineApi->isInstanceValid(
                       m_engineApi->userData, m_instanceHandle, &isValid) ==
                       Core::Native::ScriptAbiResult::Ok &&
                   isValid != 0u;
        }

        /// @brief 対象 class が公開した引数なし関数を名前で呼び出す
        [[nodiscard]] Core::Native::ScriptAbiResult
        invoke(std::string_view a_functionName) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->invokeFunction == nullptr ||
                a_functionName.empty())
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            return m_engineApi->invokeFunction(
                m_engineApi->userData, m_instanceHandle,
                Core::Native::ScriptStringView{
                    a_functionName.data(),
                    static_cast<uint32_t>(a_functionName.size())});
        }

        explicit operator bool() const noexcept
        {
            return is_valid();
        }

    private:
        const Core::Native::ScriptEngineApi* m_engineApi = nullptr;
        Core::Native::ScriptInstanceHandle m_instanceHandle{};
    };

    /// @brief Scene field として保存できる Entity 参照から ScriptRef を遅延解決する値
    template <typename T>
    class ScriptObjectPtr final
    {
    public:
        ScriptObjectPtr() = default;

        ScriptObjectPtr(const Core::Native::ScriptEngineApi* a_engineApi,
                        Core::Native::ScriptEntityHandle a_entity) noexcept
            : m_engineApi(a_engineApi), m_entity(a_entity)
        {
        }

        [[nodiscard]] Core::Native::ScriptEntityHandle entity() const noexcept
        {
            return m_entity;
        }

        [[nodiscard]] ScriptRef<T> script_ref() const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->findInstance == nullptr)
            {
                return {};
            }

            constexpr std::string_view className = ScriptTypeInfo<T>::class_name();
            Core::Native::ScriptInstanceHandle instanceHandle{};
            const Core::Native::ScriptAbiResult result =
                m_engineApi->findInstance(
                    m_engineApi->userData, m_entity,
                    Core::Native::ScriptStringView{
                        className.data(), static_cast<uint32_t>(className.size())},
                    &instanceHandle);
            return result == Core::Native::ScriptAbiResult::Ok
                       ? ScriptRef<T>(m_engineApi, instanceHandle)
                       : ScriptRef<T>{};
        }

        explicit operator bool() const noexcept
        {
            return static_cast<bool>(script_ref());
        }

    private:
        const Core::Native::ScriptEngineApi* m_engineApi = nullptr;
        Core::Native::ScriptEntityHandle m_entity{};
    };

    namespace Detail
    {
        template <typename T>
        struct MemberPointerTraits;

        template <typename TOwner, typename TValue>
        struct MemberPointerTraits<TValue TOwner::*>
        {
            using Owner = TOwner;
            using Value = TValue;
        };

        template <typename T>
        struct FunctionPointerTraits;

        template <typename TOwner>
        struct FunctionPointerTraits<void (TOwner::*)()>
        {
            using Owner = TOwner;
        };

        template <typename TOwner>
        struct FunctionPointerTraits<void (TOwner::*)() noexcept>
        {
            using Owner = TOwner;
        };

        template <typename T>
        struct IsScriptObjectPtr : std::false_type
        {
        };

        template <typename T>
        struct IsScriptObjectPtr<ScriptObjectPtr<T>> : std::true_type
        {
            using ScriptType = T;
        };

        template <typename T>
        inline constexpr bool k_alwaysFalse = false;

        template <typename T>
        struct EmptySavedState final
        {
            uint8_t reserved = 0u;
        };

        template <typename T, typename = void>
        struct SavedStateType
        {
            using Type = EmptySavedState<T>;
        };

        template <typename T>
        struct SavedStateType<T, std::void_t<typename T::SavedState>>
        {
            using Type = typename T::SavedState;
        };

        template <typename T>
        using saved_state_t = typename SavedStateType<T>::Type;

        template <typename T>
        [[nodiscard]] constexpr uint32_t state_version() noexcept
        {
            if constexpr (requires { T::k_stateVersion; })
            {
                return T::k_stateVersion;
            }
            else
            {
                return 1u;
            }
        }

        [[nodiscard]] constexpr uint64_t hash_string(std::string_view a_value) noexcept
        {
            uint64_t hash = 14695981039346656037ull;
            for (const char character : a_value)
            {
                hash ^= static_cast<uint8_t>(character);
                hash *= 1099511628211ull;
            }
            return hash;
        }

        template <typename T>
        [[nodiscard]] Core::Native::ScriptAbiResult save_state(
            const T& a_state, saved_state_t<T>& a_outState) noexcept
        {
            if constexpr (requires { typename T::SavedState; })
            {
                if constexpr (requires { a_state.save_state(a_outState); })
                {
                    a_state.save_state(a_outState);
                    return Core::Native::ScriptAbiResult::Ok;
                }
                else
                {
                    static_assert(k_alwaysFalse<T>,
                                  "Script with SavedState must implement save_state(SavedState&).");
                }
            }
            else
            {
                (void)a_state;
                (void)a_outState;
                return Core::Native::ScriptAbiResult::Ok;
            }
        }

        template <typename T>
        [[nodiscard]] Core::Native::ScriptAbiResult restore_state(
            T& a_state, const saved_state_t<T>& a_savedState) noexcept
        {
            if constexpr (requires { typename T::SavedState; })
            {
                if constexpr (requires { a_state.restore_state(a_savedState); })
                {
                    a_state.restore_state(a_savedState);
                    return Core::Native::ScriptAbiResult::Ok;
                }
                else
                {
                    static_assert(
                        k_alwaysFalse<T>,
                        "Script with SavedState must implement restore_state(const SavedState&).");
                }
            }
            else
            {
                (void)a_state;
                (void)a_savedState;
                return Core::Native::ScriptAbiResult::Ok;
            }
        }

        template <typename T>
        [[nodiscard]] std::span<const Core::Native::ScriptFieldDefinition>
        fields() noexcept
        {
            if constexpr (requires { T::script_fields(); })
            {
                return T::script_fields();
            }
            else
            {
                return {};
            }
        }

        template <typename T>
        [[nodiscard]] std::span<const Core::Native::ScriptFunctionDefinition>
        functions() noexcept
        {
            if constexpr (requires { T::script_functions(); })
            {
                return T::script_functions();
            }
            else
            {
                return {};
            }
        }
    } // namespace Detail

    /// @brief reload 対象は明示された SavedState に限定し、vtable や参照を byte 化しない
    template <typename T>
    struct StateBlob final
    {
        uint32_t version = Detail::state_version<T>();
        typename Detail::saved_state_t<T> savedState{};
    };

    template <auto TMember>
    [[nodiscard]] Core::Native::ScriptFieldDefinition make_script_field(
        std::string_view a_name,
        const typename Detail::MemberPointerTraits<decltype(TMember)>::Value& a_defaultValue,
        uint32_t a_flags = Core::Native::ScriptFieldFlagEditAnywhere |
                           Core::Native::ScriptFieldFlagSerialize) noexcept
    {
        using Traits = Detail::MemberPointerTraits<decltype(TMember)>;
        using Owner = typename Traits::Owner;
        using Value = typename Traits::Value;

        Core::Native::ScriptFieldValue defaultValue{};
        defaultValue.name = {
            a_name.data(), static_cast<uint32_t>(a_name.size())};
        defaultValue.flags = a_flags;
        if constexpr (std::is_same_v<Value, float>)
        {
            defaultValue.type = Core::Native::ScriptFieldType::Float;
            defaultValue.floatValue = a_defaultValue;
        }
        else if constexpr (std::is_same_v<Value, int32_t>)
        {
            defaultValue.type = Core::Native::ScriptFieldType::Int32;
            defaultValue.int32Value = a_defaultValue;
        }
        else if constexpr (std::is_same_v<Value, bool>)
        {
            defaultValue.type = Core::Native::ScriptFieldType::Bool;
            defaultValue.boolValue = a_defaultValue ? 1u : 0u;
        }
        else if constexpr (std::is_same_v<Value, Core::Native::ScriptEntityHandle>)
        {
            defaultValue.type = Core::Native::ScriptFieldType::Entity;
            defaultValue.entityValue = a_defaultValue;
        }
        else if constexpr (Detail::IsScriptObjectPtr<Value>::value)
        {
            using ScriptType = typename Detail::IsScriptObjectPtr<Value>::ScriptType;
            constexpr std::string_view className =
                ScriptTypeInfo<ScriptType>::class_name();
            defaultValue.type = Core::Native::ScriptFieldType::Script;
            defaultValue.entityValue = a_defaultValue.entity();
            defaultValue.classValue = {
                className.data(), static_cast<uint32_t>(className.size())};
        }
        else
        {
            static_assert(Detail::k_alwaysFalse<Value>,
                          "Unsupported Marionnette public field type.");
        }

        return Core::Native::ScriptFieldDefinition{
            defaultValue,
            [](void* a_state, const Core::Native::ScriptEngineApi* a_engineApi,
               const Core::Native::ScriptFieldValue* a_fieldValue) noexcept
                -> Core::Native::ScriptAbiResult
            {
                (void)a_engineApi;
                if (a_state == nullptr || a_fieldValue == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InvalidArgument;
                }

                auto* state = static_cast<Owner*>(a_state);
                if constexpr (std::is_same_v<Value, float>)
                {
                    state->*TMember = a_fieldValue->floatValue;
                }
                else if constexpr (std::is_same_v<Value, int32_t>)
                {
                    state->*TMember = a_fieldValue->int32Value;
                }
                else if constexpr (std::is_same_v<Value, bool>)
                {
                    state->*TMember = a_fieldValue->boolValue != 0u;
                }
                else if constexpr (
                    std::is_same_v<Value, Core::Native::ScriptEntityHandle>)
                {
                    state->*TMember = a_fieldValue->entityValue;
                }
                else if constexpr (Detail::IsScriptObjectPtr<Value>::value)
                {
                    state->*TMember =
                        Value(a_engineApi, a_fieldValue->entityValue);
                }
                return Core::Native::ScriptAbiResult::Ok;
            }};
    }

    template <auto TFunction>
    [[nodiscard]] Core::Native::ScriptFunctionDefinition
    make_script_function(std::string_view a_name) noexcept
    {
        return Core::Native::ScriptFunctionDefinition{
            Core::Native::ScriptStringView{
                a_name.data(), static_cast<uint32_t>(a_name.size())},
            [](void* a_state) noexcept -> Core::Native::ScriptAbiResult
            {
                if (a_state == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InvalidArgument;
                }

                using Owner =
                    typename Detail::FunctionPointerTraits<decltype(TFunction)>::Owner;
                auto* state = static_cast<Owner*>(a_state);
                Core::Native::ScriptAbiResult result =
                    state->begin_lifecycle(0.0f);
                if (result != Core::Native::ScriptAbiResult::Ok)
                {
                    return result;
                }

                (state->*TFunction)();
                return state->end_lifecycle();
            }};
    }

    /// @brief GameScript がアタッチ先 Entity を操作する lifecycle 基底
    class MarionnetteBehaviour
    {
    public:
        MarionnetteBehaviour(const Core::Native::ScriptEngineApi* a_engineApi,
                             Core::Native::ScriptEntityHandle a_entity) noexcept
            : m_engineApi(a_engineApi), m_entity(a_entity)
        {
        }
        virtual ~MarionnetteBehaviour() = default;

        MarionnetteBehaviour(const MarionnetteBehaviour&) = delete;
        MarionnetteBehaviour& operator=(const MarionnetteBehaviour&) = delete;
        MarionnetteBehaviour(MarionnetteBehaviour&&) = delete;
        MarionnetteBehaviour& operator=(MarionnetteBehaviour&&) = delete;

        /// @brief Script instance 生成後に一度だけ呼び出す lifecycle
        virtual void start() noexcept
        {
        }

        /// @brief ScriptSystem の更新周期で呼び出す lifecycle
        virtual void update(float a_deltaTimeSeconds) noexcept
        {
            (void)a_deltaTimeSeconds;
        }

        /// @brief Script instance を破棄する直前に一度だけ呼び出す lifecycle
        virtual void on_destroy() noexcept
        {
        }

    protected:
        /// @brief アタッチ先 Entity のローカル Transform
        ///
        /// lifecycle の前後で自動同期し、Script から明示的な取得・反映を不要にする
        Transform transform{};

        /// @brief 現在の Script 更新に渡された経過秒数
        [[nodiscard]] float delta_time() const noexcept
        {
            return m_deltaTimeSeconds;
        }

        /// @brief アタッチ先 Entity の指定 Script を現在の runtime から解決する
        template <typename T>
        [[nodiscard]] ScriptRef<T> get_script() const noexcept
        {
            return find_script<T>(m_entity);
        }

        /// @brief 指定 Entity の Script を class 型から解決する
        template <typename T>
        [[nodiscard]] ScriptRef<T> find_script(
            Core::Native::ScriptEntityHandle a_entity) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->findInstance == nullptr)
            {
                return {};
            }

            constexpr std::string_view className = ScriptTypeInfo<T>::class_name();
            Core::Native::ScriptInstanceHandle instanceHandle{};
            const Core::Native::ScriptAbiResult result =
                m_engineApi->findInstance(
                    m_engineApi->userData, a_entity,
                    Core::Native::ScriptStringView{
                        className.data(), static_cast<uint32_t>(className.size())},
                    &instanceHandle);
            return result == Core::Native::ScriptAbiResult::Ok
                       ? ScriptRef<T>(m_engineApi, instanceHandle)
                       : ScriptRef<T>{};
        }

    private:
        template <typename T>
        friend Core::Native::ScriptClassDefinition
        make_marionnette_script_class_definition(std::string_view a_className) noexcept;

        template <auto TFunction>
        friend Core::Native::ScriptFunctionDefinition
        make_script_function(std::string_view a_name) noexcept;

        [[nodiscard]] Core::Native::ScriptAbiResult begin_lifecycle(
            float a_deltaTimeSeconds) noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->readTransform == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            m_deltaTimeSeconds = a_deltaTimeSeconds;
            const Core::Native::ScriptAbiResult result = m_engineApi->readTransform(
                m_engineApi->userData, m_entity, &transform);
            if (result == Core::Native::ScriptAbiResult::Ok)
            {
                m_transformSnapshot = transform;
            }
            return result;
        }

        [[nodiscard]] Core::Native::ScriptAbiResult end_lifecycle() noexcept
        {
            if (transforms_equal(transform, m_transformSnapshot))
            {
                return Core::Native::ScriptAbiResult::Ok;
            }
            if (m_engineApi == nullptr || m_engineApi->writeTransform == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            const Core::Native::ScriptAbiResult result = m_engineApi->writeTransform(
                m_engineApi->userData, m_entity, &transform);
            if (result == Core::Native::ScriptAbiResult::Ok)
            {
                m_transformSnapshot = transform;
            }
            return result;
        }

        [[nodiscard]] static bool transforms_equal(
            const Core::Native::ScriptTransform& a_left,
            const Core::Native::ScriptTransform& a_right) noexcept
        {
            return a_left.position.x == a_right.position.x &&
                   a_left.position.y == a_right.position.y &&
                   a_left.position.z == a_right.position.z &&
                   a_left.rotation.x == a_right.rotation.x &&
                   a_left.rotation.y == a_right.rotation.y &&
                   a_left.rotation.z == a_right.rotation.z &&
                   a_left.scale.x == a_right.scale.x &&
                   a_left.scale.y == a_right.scale.y &&
                   a_left.scale.z == a_right.scale.z;
        }

        Core::Native::ScriptTransform m_transformSnapshot{};
        const Core::Native::ScriptEngineApi* m_engineApi = nullptr;
        Core::Native::ScriptEntityHandle m_entity{};
        float m_deltaTimeSeconds = 0.0f;
    };

    template <typename T>
    [[nodiscard]] Core::Native::ScriptClassDefinition
    make_marionnette_script_class_definition(std::string_view a_className) noexcept
    {
        static_assert(std::is_base_of_v<MarionnetteBehaviour, T>);
        static_assert(std::is_trivially_copyable_v<StateBlob<T>>,
                      "SavedState must be trivially copyable for DLL reload.");

        const std::span<const Core::Native::ScriptFieldDefinition> fields =
            Detail::fields<T>();
        const std::span<const Core::Native::ScriptFunctionDefinition> functions =
            Detail::functions<T>();

        Core::Native::ScriptClassDefinition definition{};
        definition.className = {
            a_className.data(), static_cast<uint32_t>(a_className.size())};
        definition.createState =
            [](const Core::Native::ScriptEngineApi* a_engineApi,
               const Core::Native::ScriptCreateInfo* a_createInfo,
               void** a_outState) noexcept -> Core::Native::ScriptAbiResult
        {
            if (a_engineApi == nullptr || a_createInfo == nullptr ||
                a_outState == nullptr ||
                (a_createInfo->fieldCount > 0u &&
                 a_createInfo->fieldValues == nullptr))
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            T* state = new (std::nothrow) T(a_engineApi, a_createInfo->entity);
            if (state == nullptr)
            {
                return Core::Native::ScriptAbiResult::InternalError;
            }

            const std::span<const Core::Native::ScriptFieldDefinition>
                fieldDefinitions = Detail::fields<T>();
            for (const Core::Native::ScriptFieldDefinition& field :
                 fieldDefinitions)
            {
                Core::Native::ScriptAbiResult result =
                    field.applyValue(state, a_engineApi, &field.defaultValue);
                if (result != Core::Native::ScriptAbiResult::Ok)
                {
                    delete state;
                    return result;
                }
            }

            for (uint32_t valueIndex = 0u;
                 valueIndex < a_createInfo->fieldCount; ++valueIndex)
            {
                const Core::Native::ScriptFieldValue& value =
                    a_createInfo->fieldValues[valueIndex];
                for (const Core::Native::ScriptFieldDefinition& field :
                     fieldDefinitions)
                {
                    if (field.defaultValue.type == value.type &&
                        field.defaultValue.name.size == value.name.size &&
                        std::string_view(
                            field.defaultValue.name.data,
                            field.defaultValue.name.size) ==
                            std::string_view(value.name.data, value.name.size))
                    {
                        const Core::Native::ScriptAbiResult result =
                            field.applyValue(state, a_engineApi, &value);
                        if (result != Core::Native::ScriptAbiResult::Ok)
                        {
                            delete state;
                            return result;
                        }
                        break;
                    }
                }
            }

            *a_outState = state;
            return Core::Native::ScriptAbiResult::Ok;
        };
        definition.destroyState = [](void* a_state) noexcept
        {
            auto* state = static_cast<T*>(a_state);
            if (state != nullptr)
            {
                state->on_destroy();
                delete state;
            }
        };
        definition.startState =
            [](void* a_state) noexcept -> Core::Native::ScriptAbiResult
        {
            if (a_state == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            auto* state = static_cast<T*>(a_state);
            Core::Native::ScriptAbiResult result =
                state->begin_lifecycle(0.0f);
            if (result != Core::Native::ScriptAbiResult::Ok)
            {
                return result;
            }

            state->start();
            return state->end_lifecycle();
        };
        definition.updateState =
            [](void* a_state,
               float a_deltaTimeSeconds) noexcept
                -> Core::Native::ScriptAbiResult
        {
            if (a_state == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            auto* state = static_cast<T*>(a_state);
            Core::Native::ScriptAbiResult result =
                state->begin_lifecycle(a_deltaTimeSeconds);
            if (result != Core::Native::ScriptAbiResult::Ok)
            {
                return result;
            }

            state->update(a_deltaTimeSeconds);
            return state->end_lifecycle();
        };
        definition.fields = fields.data();
        definition.fieldCount = static_cast<uint32_t>(fields.size());
        definition.functions = functions.data();
        definition.functionCount = static_cast<uint32_t>(functions.size());
        definition.stateDescriptor = Core::Native::ScriptStateDescriptor{
            Detail::state_version<T>(),
            static_cast<uint32_t>(sizeof(StateBlob<T>)),
            Detail::hash_string(a_className) ^
                (static_cast<uint64_t>(Detail::state_version<T>()) << 32u) ^
                sizeof(StateBlob<T>)};
        definition.getStateSize = []() noexcept -> uint32_t
        {
            return static_cast<uint32_t>(sizeof(StateBlob<T>));
        };
        definition.serializeState =
            [](const void* a_state, void* a_outStateBuffer,
               uint32_t a_stateBufferSize) noexcept
                -> Core::Native::ScriptAbiResult
        {
            if (a_state == nullptr || a_outStateBuffer == nullptr ||
                a_stateBufferSize != sizeof(StateBlob<T>))
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            StateBlob<T> stateBlob{};
            const Core::Native::ScriptAbiResult result =
                Detail::save_state(
                    *static_cast<const T*>(a_state), stateBlob.savedState);
            if (result != Core::Native::ScriptAbiResult::Ok)
            {
                return result;
            }

            std::memcpy(a_outStateBuffer, &stateBlob, sizeof(stateBlob));
            return Core::Native::ScriptAbiResult::Ok;
        };
        definition.restoreState =
            [](void* a_state, const void* a_stateBuffer,
               uint32_t a_stateBufferSize) noexcept
                -> Core::Native::ScriptAbiResult
        {
            if (a_state == nullptr || a_stateBuffer == nullptr ||
                a_stateBufferSize != sizeof(StateBlob<T>))
            {
                return Core::Native::ScriptAbiResult::InvalidArgument;
            }

            StateBlob<T> stateBlob{};
            std::memcpy(&stateBlob, a_stateBuffer, sizeof(stateBlob));
            if (stateBlob.version != Detail::state_version<T>())
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return Detail::restore_state(
                *static_cast<T*>(a_state), stateBlob.savedState);
        };
        return definition;
    }
} // namespace Cue::Script

#define MARIONETTE_DECLARE_SCRIPT_TYPE(a_scriptType, a_className) \
    template <> \
    struct Cue::Script::ScriptTypeInfo<a_scriptType> \
    { \
        [[nodiscard]] static constexpr std::string_view class_name() noexcept \
        { \
            return a_className; \
        } \
    }

#define MARIONETTE_FIELD(a_scriptType, a_member, a_defaultValue) \
    ::Cue::Script::make_script_field<&a_scriptType::a_member>( \
        #a_member, a_defaultValue)

#define MARIONETTE_FIELD_META( \
    a_scriptType, a_member, a_defaultValue, a_flags) \
    ::Cue::Script::make_script_field<&a_scriptType::a_member>( \
        #a_member, a_defaultValue, a_flags)

#define MARIONETTE_FIELDS(...) \
    [[nodiscard]] static std::span< \
        const ::Cue::Core::Native::ScriptFieldDefinition> \
    script_fields() noexcept \
    { \
        static const auto k_fields = std::array{__VA_ARGS__}; \
        return std::span<const ::Cue::Core::Native::ScriptFieldDefinition>( \
            k_fields.data(), k_fields.size()); \
    }

#define MARIONETTE_NO_FIELDS() \
    [[nodiscard]] static std::span< \
        const ::Cue::Core::Native::ScriptFieldDefinition> \
    script_fields() noexcept \
    { \
        return {}; \
    }

#define MARIONETTE_FUNCTION(a_scriptType, a_function) \
    ::Cue::Script::make_script_function<&a_scriptType::a_function>(#a_function)

#define MARIONETTE_FUNCTIONS(...) \
    [[nodiscard]] static std::span< \
        const ::Cue::Core::Native::ScriptFunctionDefinition> \
    script_functions() noexcept \
    { \
        static const auto k_functions = std::array{__VA_ARGS__}; \
        return std::span<const ::Cue::Core::Native::ScriptFunctionDefinition>( \
            k_functions.data(), k_functions.size()); \
    }

#define MARIONETTE_NO_FUNCTIONS() \
    [[nodiscard]] static std::span< \
        const ::Cue::Core::Native::ScriptFunctionDefinition> \
    script_functions() noexcept \
    { \
        return {}; \
    }
