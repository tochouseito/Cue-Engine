#pragma once

#include <Native/ScriptAbi.h>
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <array>
#include <concepts>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace Marionette
{
    using Cue::Core::Native::ScriptClassDefinition;
    using Cue::Core::Native::ScriptFieldReader;
    using Cue::Core::Native::hash_script_schema;
    using Cue::Core::Native::make_script_string_view;

    using Transform = CueTransformData;

    template<typename T>
    struct ScriptTypeInfo;

    template<typename T>
    class ScriptRef;

    template<typename... TFieldValues>
    [[nodiscard]] constexpr auto make_fields(
        TFieldValues... a_fieldValues) noexcept
    {
        return std::array<CueScriptFieldValue, sizeof...(TFieldValues)>{
            a_fieldValues...
        };
    }

    namespace Detail
    {
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

    private:
        const CueEngineApi* m_engineApi = nullptr;
        CueScriptInstanceHandle m_instanceHandle{ k_cueInvalidHandleValue };
    };

    template<typename T>
    class Behaviour
    {
    public:
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

            if (!m_hasStarted)
            {
                const CueResult startResult =
                    Detail::invoke_start(static_cast<T&>(*this));
                if (startResult != CueResult_Ok)
                {
                    m_engineApi = nullptr;
                    return startResult;
                }

                m_hasStarted = true;
            }

            const CueResult updateResult =
                Detail::invoke_update(
                    static_cast<T&>(*this),
                    a_deltaTimeSeconds);
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

    protected:
        using Transform = Marionette::Transform;

        [[nodiscard]] CueEntityHandle entity_handle() const noexcept
        {
            return m_entityHandle;
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> get_script() const noexcept
        {
            return find_script<TOtherScript>(m_entityHandle);
        }

        template<typename TOtherScript>
        [[nodiscard]] ScriptRef<TOtherScript> find_script(
            CueEntityHandle a_entityHandle) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->findScriptInstance == nullptr ||
                a_entityHandle.value == k_cueInvalidHandleValue)
            {
                return {};
            }

            CueScriptInstanceHandle instanceHandle{ k_cueInvalidHandleValue };
            if (m_engineApi->findScriptInstance(
                    a_entityHandle,
                    Detail::to_cue_string_view(
                        Detail::class_name<TOtherScript>()),
                    &instanceHandle) != CueResult_Ok)
            {
                return {};
            }

            return ScriptRef<TOtherScript>(m_engineApi, instanceHandle);
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
            return m_engineApi != nullptr &&
                m_engineApi->isEntityValid != nullptr &&
                m_engineApi->isEntityValid(m_entityHandle) != 0;
        }

        [[nodiscard]] bool has_transform() const noexcept
        {
            return m_engineApi != nullptr &&
                m_engineApi->hasTransform != nullptr &&
                m_engineApi->hasTransform(m_entityHandle) != 0;
        }

        [[nodiscard]] CueResult get_transform(
            Transform& a_outTransform) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->getTransform == nullptr)
            {
                return CueResult_InvalidState;
            }

            return m_engineApi->getTransform(m_entityHandle, &a_outTransform);
        }

        [[nodiscard]] CueResult set_transform(
            const Transform& a_transform) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->setTransform == nullptr)
            {
                return CueResult_InvalidState;
            }

            return m_engineApi->setTransform(m_entityHandle, &a_transform);
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

    private:
        void log(
            CueLogSeverity a_severity,
            std::string_view a_message) const noexcept
        {
            if (m_engineApi == nullptr ||
                m_engineApi->log == nullptr)
            {
                return;
            }

            (void)m_engineApi->log(
                a_severity,
                make_script_string_view(
                    a_message.data(),
                    static_cast<uint32_t>(a_message.size())));
        }

        const CueEngineApi* m_engineApi = nullptr;
        CueEntityHandle m_entityHandle{ k_cueInvalidHandleValue };
        bool m_hasStarted = false;
    };

    template<typename T>
    [[nodiscard]] ScriptClassDefinition make_script_definition() noexcept
    {
        const std::span<const CueScriptFieldValue> fields =
            Detail::fields<T>();

        return ScriptClassDefinition{
            make_script_string_view(
                Detail::class_name<T>().data(),
                static_cast<uint32_t>(Detail::class_name<T>().size())),
            fields.data(),
            static_cast<uint32_t>(fields.size()),
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

#define MARIONETTE_DECLARE_SCRIPT_TYPE(a_typeName, a_classNameLiteral) \
    class a_typeName; \
    template<> \
    struct ::Marionette::ScriptTypeInfo<a_typeName> final \
    { \
        static constexpr std::string_view k_className = a_classNameLiteral; \
    }
