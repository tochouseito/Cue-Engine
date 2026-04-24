#pragma once

// === Core includes ===
#include "ScriptAbi.h"

// === C++ includes ===
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>

namespace Cue::Core::Native
{
    using CueScriptCreateStateFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueEngineApi* a_engineApi,
        const CueScriptCreateInfo* a_createInfo,
        void** a_outState
    );

    using CueScriptDestroyStateFn =
        void (CUE_SCRIPT_CALL*)(void* a_state);

    using CueScriptUpdateStateFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueEngineApi* a_engineApi,
        void* a_state,
        float a_deltaTimeSeconds
    );

    using CueScriptInvokeStateFn = CueResult (CUE_SCRIPT_CALL*)(
        const CueEngineApi* a_engineApi,
        void* a_state
    );

    using CueScriptSerializeStateFn = CueResult (CUE_SCRIPT_CALL*)(
        const void* a_state,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize
    );

    using CueScriptRestoreStateFn = CueResult (CUE_SCRIPT_CALL*)(
        void* a_state,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize
    );

    struct ScriptFunctionBinding final
    {
        CueScriptFunctionDefinition definition{};
        CueScriptInvokeStateFn invoke = nullptr;
    };

    struct ScriptClassDefinition final
    {
        CueStringView className{};
        const CueScriptFieldValue* fieldValues = nullptr;
        uint32_t fieldValueCount = 0;
        const ScriptFunctionBinding* functionBindings = nullptr;
        uint32_t functionBindingCount = 0;
        CueScriptStateDescriptor stateDescriptor{};
        CueScriptCreateStateFn createState = nullptr;
        CueScriptDestroyStateFn destroyState = nullptr;
        CueScriptUpdateStateFn updateState = nullptr;
        CueScriptSerializeStateFn serializeState = nullptr;
        CueScriptRestoreStateFn restoreState = nullptr;
    };

    [[nodiscard]] constexpr CueStringView make_script_string_view(
        const char* a_text,
        uint32_t a_size) noexcept;

    [[nodiscard]] constexpr uint64_t hash_script_schema(
        std::string_view a_text) noexcept;

    [[nodiscard]] inline bool string_view_equals(
        CueStringView a_left,
        CueStringView a_right) noexcept;

    class ScriptFieldReader final
    {
    public:
        explicit ScriptFieldReader(const CueScriptCreateInfo* a_createInfo) noexcept
            : m_createInfo(a_createInfo)
        {
        }

        [[nodiscard]] CueEntityHandle entity_handle() const noexcept
        {
            return m_createInfo != nullptr
                ? m_createInfo->entityHandle
                : CueEntityHandle{ k_cueInvalidHandleValue };
        }

        [[nodiscard]] const CueScriptFieldValue* find(
            CueStringView a_fieldName) const noexcept
        {
            if (m_createInfo == nullptr ||
                m_createInfo->fieldValues == nullptr ||
                m_createInfo->fieldValueCount == 0)
            {
                return nullptr;
            }

            for (uint32_t fieldIndex = 0;
                 fieldIndex < m_createInfo->fieldValueCount;
                 ++fieldIndex)
            {
                const CueScriptFieldValue& fieldValue =
                    m_createInfo->fieldValues[fieldIndex];
                if (string_view_equals(fieldValue.name, a_fieldName))
                {
                    return &fieldValue;
                }
            }

            return nullptr;
        }

        [[nodiscard]] bool read_float(
            CueStringView a_fieldName,
            float& a_outValue) const noexcept
        {
            const CueScriptFieldValue* fieldValue = find(a_fieldName);
            if (fieldValue == nullptr ||
                fieldValue->type != CueScriptFieldType_Float)
            {
                return false;
            }

            a_outValue = fieldValue->floatValue;
            return true;
        }

        [[nodiscard]] bool read_int32(
            CueStringView a_fieldName,
            int32_t& a_outValue) const noexcept
        {
            const CueScriptFieldValue* fieldValue = find(a_fieldName);
            if (fieldValue == nullptr ||
                fieldValue->type != CueScriptFieldType_Int32)
            {
                return false;
            }

            a_outValue = fieldValue->intValue;
            return true;
        }

        [[nodiscard]] bool read_bool(
            CueStringView a_fieldName,
            bool& a_outValue) const noexcept
        {
            const CueScriptFieldValue* fieldValue = find(a_fieldName);
            if (fieldValue == nullptr ||
                fieldValue->type != CueScriptFieldType_Bool)
            {
                return false;
            }

            a_outValue = fieldValue->boolValue != 0;
            return true;
        }

        [[nodiscard]] bool read_entity_handle(
            CueStringView a_fieldName,
            CueEntityHandle& a_outValue) const noexcept
        {
            const CueScriptFieldValue* fieldValue = find(a_fieldName);
            if (fieldValue == nullptr ||
                fieldValue->type != CueScriptFieldType_EntityRef)
            {
                return false;
            }

            a_outValue = fieldValue->entityValue;
            return true;
        }

        [[nodiscard]] bool read_script_class_name(
            CueStringView a_fieldName,
            CueStringView& a_outValue) const noexcept
        {
            const CueScriptFieldValue* fieldValue = find(a_fieldName);
            if (fieldValue == nullptr ||
                fieldValue->type != CueScriptFieldType_ClassRef)
            {
                return false;
            }

            a_outValue = fieldValue->classValue;
            return true;
        }

    private:
        const CueScriptCreateInfo* m_createInfo = nullptr;
    };

    template<typename T>
    concept ScriptStateHasFields = requires
    {
        { T::script_fields() } -> std::convertible_to<std::span<const CueScriptFieldValue>>;
    };

    template<typename T>
    concept ScriptStateHasFunctions = requires
    {
        { T::script_functions() } -> std::convertible_to<std::span<const ScriptFunctionBinding>>;
    };

    template<typename T>
    concept ScriptStateHasBlobType = requires
    {
        typename T::StateBlob;
    };

    template<typename T>
    concept ScriptStateHasCreate = requires(
        const CueScriptCreateInfo* a_createInfo,
        T& a_state)
    {
        { T::create(a_createInfo, a_state) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    concept ScriptStateHasUpdate = requires(
        const CueEngineApi* a_engineApi,
        T& a_state,
        float a_deltaTimeSeconds)
    {
        { a_state.update(a_engineApi, a_deltaTimeSeconds) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    concept ScriptStateHasRawSerialize = requires(
        const T& a_state,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize)
    {
        { a_state.serialize(a_outStateBuffer, a_stateBufferSize) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    concept ScriptStateHasRawRestore = requires(
        T& a_state,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize)
    {
        { a_state.restore(a_stateBuffer, a_stateBufferSize) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    using script_state_blob_type_t = std::conditional_t<
        ScriptStateHasBlobType<T>,
        typename T::StateBlob,
        T>;

    template<typename T>
    concept ScriptStateHasBlobSerialize = requires(
        const T& a_state,
        script_state_blob_type_t<T>& a_stateBlob)
    {
        { a_state.serialize(a_stateBlob) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    concept ScriptStateHasBlobRestore = requires(
        T& a_state,
        const script_state_blob_type_t<T>& a_stateBlob)
    {
        { a_state.restore(a_stateBlob) } -> std::convertible_to<CueResult>;
    };

    template<typename T>
    [[nodiscard]] CueResult CUE_SCRIPT_CALL create_script_state_adapter(
        const CueEngineApi*,
        const CueScriptCreateInfo* a_createInfo,
        void** a_outState)
    {
        static_assert(ScriptStateHasCreate<T>,
            "Script state must implement static CueResult create(const CueScriptCreateInfo*, T&).");

        if (a_createInfo == nullptr || a_outState == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        auto* state = new (std::nothrow) T{};
        if (state == nullptr)
        {
            return CueResult_InternalError;
        }

        const CueResult createResult = T::create(a_createInfo, *state);
        if (createResult != CueResult_Ok)
        {
            delete state;
            return createResult;
        }

        *a_outState = state;
        return CueResult_Ok;
    }

    template<typename T>
    void CUE_SCRIPT_CALL destroy_script_state_adapter(void* a_state)
    {
        delete static_cast<T*>(a_state);
    }

    template<typename T>
    [[nodiscard]] CueResult CUE_SCRIPT_CALL update_script_state_adapter(
        const CueEngineApi* a_engineApi,
        void* a_state,
        float a_deltaTimeSeconds)
    {
        static_assert(ScriptStateHasUpdate<T>,
            "Script state must implement CueResult update(const CueEngineApi*, float).");

        if (a_engineApi == nullptr || a_state == nullptr)
        {
            return CueResult_InvalidState;
        }

        return static_cast<T*>(a_state)->update(a_engineApi, a_deltaTimeSeconds);
    }

    template<typename T, auto TFunction>
    [[nodiscard]] CueResult CUE_SCRIPT_CALL invoke_script_state_function_adapter(
        const CueEngineApi* a_engineApi,
        void* a_state)
    {
        using FunctionType = decltype(TFunction);

        if (a_engineApi == nullptr || a_state == nullptr)
        {
            return CueResult_InvalidState;
        }

        static_assert(
            std::is_same_v<FunctionType, CueResult (T::*)()> ||
            std::is_same_v<FunctionType, void (T::*)()>,
            "Script function must be void() or CueResult().");

        T* state = static_cast<T*>(a_state);
        if constexpr (requires
            {
                state->begin_script_call(a_engineApi);
            })
        {
            state->begin_script_call(a_engineApi);
        }

        if constexpr (std::is_same_v<FunctionType, CueResult (T::*)()>)
        {
            const CueResult result = (state->*TFunction)();
            if constexpr (requires
                {
                    state->end_script_call();
                })
            {
                state->end_script_call();
            }

            return result;
        }
        else
        {
            (state->*TFunction)();
            if constexpr (requires
                {
                    state->end_script_call();
                })
            {
                state->end_script_call();
            }

            return CueResult_Ok;
        }
    }

    template<typename T>
    [[nodiscard]] CueResult CUE_SCRIPT_CALL serialize_script_state_adapter(
        const void* a_state,
        void* a_outStateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_outStateBuffer == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        if constexpr (ScriptStateHasRawSerialize<T>)
        {
            return static_cast<const T*>(a_state)->serialize(
                a_outStateBuffer, a_stateBufferSize);
        }
        else if constexpr (ScriptStateHasBlobSerialize<T>)
        {
            using StateBlob = script_state_blob_type_t<T>;

            if (a_stateBufferSize != sizeof(StateBlob))
            {
                return CueResult_InvalidArgument;
            }

            StateBlob blob{};
            const CueResult serializeResult =
                static_cast<const T*>(a_state)->serialize(blob);
            if (serializeResult != CueResult_Ok)
            {
                return serializeResult;
            }

            std::memcpy(a_outStateBuffer, &blob, sizeof(StateBlob));
            return CueResult_Ok;
        }
        else if constexpr (!ScriptStateHasBlobType<T> &&
            std::is_trivially_copyable_v<T>)
        {
            if (a_stateBufferSize != sizeof(T))
            {
                return CueResult_InvalidArgument;
            }

            std::memcpy(a_outStateBuffer, a_state, sizeof(T));
            return CueResult_Ok;
        }
        else
        {
            return CueResult_InvalidState;
        }
    }

    template<typename T>
    [[nodiscard]] CueResult CUE_SCRIPT_CALL restore_script_state_adapter(
        void* a_state,
        const void* a_stateBuffer,
        uint32_t a_stateBufferSize)
    {
        if (a_state == nullptr || a_stateBuffer == nullptr)
        {
            return CueResult_InvalidArgument;
        }

        if constexpr (ScriptStateHasRawRestore<T>)
        {
            return static_cast<T*>(a_state)->restore(
                a_stateBuffer, a_stateBufferSize);
        }
        else if constexpr (ScriptStateHasBlobRestore<T>)
        {
            using StateBlob = script_state_blob_type_t<T>;

            if (a_stateBufferSize != sizeof(StateBlob))
            {
                return CueResult_InvalidArgument;
            }

            StateBlob blob{};
            std::memcpy(&blob, a_stateBuffer, sizeof(StateBlob));
            return static_cast<T*>(a_state)->restore(blob);
        }
        else if constexpr (!ScriptStateHasBlobType<T> &&
            std::is_trivially_copyable_v<T>)
        {
            if (a_stateBufferSize != sizeof(T))
            {
                return CueResult_InvalidArgument;
            }

            std::memcpy(a_state, a_stateBuffer, sizeof(T));
            return CueResult_Ok;
        }
        else
        {
            return CueResult_InvalidState;
        }
    }

    template<typename T>
    [[nodiscard]] ScriptClassDefinition make_script_class_definition()
    {
        static_assert(ScriptStateHasFields<T>,
            "Script state must implement static std::span<const CueScriptFieldValue> script_fields().");

        const std::span<const CueScriptFieldValue> fields = T::script_fields();
        const std::span<const ScriptFunctionBinding> functions =
            []() noexcept
        {
            if constexpr (ScriptStateHasFunctions<T>)
            {
                return T::script_functions();
            }
            else
            {
                return std::span<const ScriptFunctionBinding>{};
            }
        }();
        using StateBlob = script_state_blob_type_t<T>;
        return ScriptClassDefinition{
            make_script_string_view(
                T::k_className.data(),
                static_cast<uint32_t>(T::k_className.size())),
            fields.data(),
            static_cast<uint32_t>(fields.size()),
            functions.data(),
            static_cast<uint32_t>(functions.size()),
            CueScriptStateDescriptor{
                static_cast<uint32_t>(T::k_stateVersion),
                static_cast<uint32_t>(sizeof(StateBlob)),
                hash_script_schema(T::k_stateSchema)
            },
            &create_script_state_adapter<T>,
            &destroy_script_state_adapter<T>,
            &update_script_state_adapter<T>,
            &serialize_script_state_adapter<T>,
            &restore_script_state_adapter<T>
        };
    }

    class ScriptModuleRuntime final
    {
    public:
        [[nodiscard]] CueResult register_scripts(const CueEngineApi* a_engineApi,
            std::span<const ScriptClassDefinition> a_scriptClasses) noexcept
        {
            if (a_engineApi == nullptr)
            {
                return CueResult_InvalidArgument;
            }

            m_engineApi = a_engineApi;
            for (const ScriptClassDefinition& scriptClass : a_scriptClasses)
            {
                CueResult result =
                    register_script_class(a_engineApi, scriptClass);
                if (result != CueResult_Ok)
                {
                    return result;
                }
            }

            return CueResult_Ok;
        }

        [[nodiscard]] CueResult create_script_instance(
            std::span<const ScriptClassDefinition> a_scriptClasses,
            const CueScriptCreateInfo* a_createInfo,
            CueScriptInstanceHandle* a_outInstanceHandle) noexcept
        {
            if (m_engineApi == nullptr)
            {
                return CueResult_InvalidState;
            }
            if (a_createInfo == nullptr || a_outInstanceHandle == nullptr)
            {
                return CueResult_InvalidArgument;
            }
            if (a_createInfo->entityHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidArgument;
            }
            if (m_engineApi->isEntityValid != nullptr &&
                m_engineApi->isEntityValid(a_createInfo->entityHandle) == 0)
            {
                return CueResult_NotFound;
            }

            const ScriptClassDefinition* scriptClass =
                find_script_class(a_scriptClasses, a_createInfo->scriptName);
            if (scriptClass == nullptr || scriptClass->createState == nullptr)
            {
                return CueResult_NotFound;
            }

            void* state = nullptr;
            const CueResult createResult =
                scriptClass->createState(m_engineApi, a_createInfo, &state);
            if (createResult != CueResult_Ok)
            {
                return createResult;
            }

            const uint64_t instanceId = m_nextInstanceId++;
            m_instances.emplace(instanceId, InstanceRecord{
                scriptClass,
                state
            });
            a_outInstanceHandle->value = instanceId;
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult destroy_script_instance(
            CueScriptInstanceHandle a_instanceHandle) noexcept
        {
            if (a_instanceHandle.value == k_cueInvalidHandleValue)
            {
                return CueResult_InvalidArgument;
            }

            const auto instanceIt = m_instances.find(a_instanceHandle.value);
            if (instanceIt == m_instances.end())
            {
                return CueResult_NotFound;
            }

            destroy_instance_record(instanceIt->second);
            m_instances.erase(instanceIt);
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult update_script_instance(
            CueScriptInstanceHandle a_instanceHandle,
            float a_deltaTimeSeconds) noexcept
        {
            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return a_instanceHandle.value == k_cueInvalidHandleValue
                    ? CueResult_InvalidArgument
                    : CueResult_NotFound;
            }
            if (m_engineApi == nullptr || instance->scriptClass == nullptr ||
                instance->scriptClass->updateState == nullptr)
            {
                return CueResult_InvalidState;
            }

            return instance->scriptClass->updateState(
                m_engineApi, instance->state, a_deltaTimeSeconds);
        }

        [[nodiscard]] CueResult invoke_script_instance_function(
            CueScriptInstanceHandle a_instanceHandle,
            CueStringView a_functionName) noexcept
        {
            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return a_instanceHandle.value == k_cueInvalidHandleValue
                    ? CueResult_InvalidArgument
                    : CueResult_NotFound;
            }
            if (m_engineApi == nullptr || instance->scriptClass == nullptr)
            {
                return CueResult_InvalidState;
            }

            const ScriptFunctionBinding* functionBinding =
                find_script_function(*instance->scriptClass, a_functionName);
            if (functionBinding == nullptr)
            {
                return CueResult_NotFound;
            }
            if (functionBinding->invoke == nullptr)
            {
                return CueResult_InvalidState;
            }

            return functionBinding->invoke(m_engineApi, instance->state);
        }

        [[nodiscard]] void* get_script_instance_object(
            CueScriptInstanceHandle a_instanceHandle) const noexcept
        {
            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return nullptr;
            }

            return instance->state;
        }

        [[nodiscard]] CueResult get_script_instance_state_size(
            CueScriptInstanceHandle a_instanceHandle,
            uint32_t* a_outStateSize) const noexcept
        {
            if (a_outStateSize == nullptr)
            {
                return CueResult_InvalidArgument;
            }

            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return a_instanceHandle.value == k_cueInvalidHandleValue
                    ? CueResult_InvalidArgument
                    : CueResult_NotFound;
            }
            if (instance->scriptClass == nullptr)
            {
                return CueResult_InvalidState;
            }

            *a_outStateSize = instance->scriptClass->stateDescriptor.stateSize;
            return CueResult_Ok;
        }

        [[nodiscard]] CueResult serialize_script_instance(
            CueScriptInstanceHandle a_instanceHandle,
            void* a_outStateBuffer,
            uint32_t a_stateBufferSize) const noexcept
        {
            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return a_instanceHandle.value == k_cueInvalidHandleValue
                    ? CueResult_InvalidArgument
                    : CueResult_NotFound;
            }
            if (instance->scriptClass == nullptr ||
                instance->scriptClass->serializeState == nullptr)
            {
                return CueResult_InvalidState;
            }
            if (a_outStateBuffer == nullptr ||
                a_stateBufferSize != instance->scriptClass->stateDescriptor.stateSize)
            {
                return CueResult_InvalidArgument;
            }

            return instance->scriptClass->serializeState(
                instance->state, a_outStateBuffer, a_stateBufferSize);
        }

        [[nodiscard]] CueResult restore_script_instance(
            CueScriptInstanceHandle a_instanceHandle,
            const void* a_stateBuffer,
            uint32_t a_stateBufferSize) noexcept
        {
            const InstanceRecord* instance = find_instance(a_instanceHandle);
            if (instance == nullptr)
            {
                return a_instanceHandle.value == k_cueInvalidHandleValue
                    ? CueResult_InvalidArgument
                    : CueResult_NotFound;
            }
            if (instance->scriptClass == nullptr ||
                instance->scriptClass->restoreState == nullptr)
            {
                return CueResult_InvalidState;
            }
            if (a_stateBuffer == nullptr ||
                a_stateBufferSize != instance->scriptClass->stateDescriptor.stateSize)
            {
                return CueResult_InvalidArgument;
            }

            return instance->scriptClass->restoreState(
                instance->state, a_stateBuffer, a_stateBufferSize);
        }

        [[nodiscard]] CueResult get_script_state_descriptor(
            std::span<const ScriptClassDefinition> a_scriptClasses,
            CueStringView a_scriptClassName,
            CueScriptStateDescriptor* a_outDescriptor) const noexcept
        {
            if (a_outDescriptor == nullptr)
            {
                return CueResult_InvalidArgument;
            }

            const ScriptClassDefinition* scriptClass =
                find_script_class(a_scriptClasses, a_scriptClassName);
            if (scriptClass == nullptr)
            {
                return CueResult_NotFound;
            }

            *a_outDescriptor = scriptClass->stateDescriptor;
            return CueResult_Ok;
        }

        [[nodiscard]] const CueEngineApi* engine_api() const noexcept
        {
            return m_engineApi;
        }

    private:
        struct InstanceRecord final
        {
            const ScriptClassDefinition* scriptClass = nullptr;
            void* state = nullptr;
        };

        [[nodiscard]] static bool supports_register_script_class(
            const CueEngineApi* a_engineApi) noexcept
        {
            return a_engineApi != nullptr &&
                a_engineApi->structSize >=
                offsetof(CueEngineApi, registerScriptClass) +
                    sizeof(CueRegisterScriptClassFn) &&
                a_engineApi->registerScriptClass != nullptr;
        }

        [[nodiscard]] static bool supports_register_script_field(
            const CueEngineApi* a_engineApi) noexcept
        {
            return a_engineApi != nullptr &&
                a_engineApi->structSize >=
                offsetof(CueEngineApi, registerScriptField) +
                    sizeof(CueRegisterScriptFieldFn) &&
                a_engineApi->registerScriptField != nullptr;
        }

        [[nodiscard]] static CueResult register_script_class(
            const CueEngineApi* a_engineApi,
            const ScriptClassDefinition& a_scriptClass) noexcept
        {
            if (supports_register_script_class(a_engineApi))
            {
                const CueResult classResult =
                    a_engineApi->registerScriptClass(a_scriptClass.className);
                if (classResult != CueResult_Ok)
                {
                    return classResult;
                }
            }

            if (supports_register_script_field(a_engineApi) &&
                a_scriptClass.fieldValues != nullptr &&
                a_scriptClass.fieldValueCount != 0)
            {
                for (uint32_t fieldIndex = 0;
                     fieldIndex < a_scriptClass.fieldValueCount;
                     ++fieldIndex)
                {
                    const CueResult fieldResult =
                        a_engineApi->registerScriptField(
                            a_scriptClass.className,
                            &a_scriptClass.fieldValues[fieldIndex]);
                    if (fieldResult != CueResult_Ok)
                    {
                        return fieldResult;
                    }
                }
            }

            if (supports_register_script_function(a_engineApi) &&
                a_scriptClass.functionBindings != nullptr &&
                a_scriptClass.functionBindingCount != 0)
            {
                for (uint32_t functionIndex = 0;
                     functionIndex < a_scriptClass.functionBindingCount;
                     ++functionIndex)
                {
                    const CueResult functionResult =
                        a_engineApi->registerScriptFunction(
                            a_scriptClass.className,
                            &a_scriptClass.functionBindings[functionIndex].definition);
                    if (functionResult != CueResult_Ok)
                    {
                        return functionResult;
                    }
                }
            }

            return CueResult_Ok;
        }

        [[nodiscard]] static bool supports_register_script_function(
            const CueEngineApi* a_engineApi) noexcept
        {
            return a_engineApi != nullptr &&
                a_engineApi->structSize >=
                offsetof(CueEngineApi, registerScriptFunction) +
                    sizeof(CueRegisterScriptFunctionFn) &&
                a_engineApi->registerScriptFunction != nullptr;
        }

        [[nodiscard]] static const ScriptClassDefinition* find_script_class(
            std::span<const ScriptClassDefinition> a_scriptClasses,
            CueStringView a_scriptClassName) noexcept
        {
            for (const ScriptClassDefinition& scriptClass : a_scriptClasses)
            {
                if (string_view_equals(scriptClass.className, a_scriptClassName))
                {
                    return &scriptClass;
                }
            }

            return nullptr;
        }

        [[nodiscard]] static const ScriptFunctionBinding* find_script_function(
            const ScriptClassDefinition& a_scriptClass,
            CueStringView a_functionName) noexcept
        {
            if (a_functionName.data == nullptr ||
                a_functionName.size == 0 ||
                a_scriptClass.functionBindings == nullptr ||
                a_scriptClass.functionBindingCount == 0)
            {
                return nullptr;
            }

            for (uint32_t functionIndex = 0;
                 functionIndex < a_scriptClass.functionBindingCount;
                 ++functionIndex)
            {
                const ScriptFunctionBinding& functionBinding =
                    a_scriptClass.functionBindings[functionIndex];
                if (string_view_equals(
                        functionBinding.definition.name,
                        a_functionName))
                {
                    return &functionBinding;
                }
            }

            return nullptr;
        }

        [[nodiscard]] const InstanceRecord* find_instance(
            CueScriptInstanceHandle a_instanceHandle) const noexcept
        {
            if (a_instanceHandle.value == k_cueInvalidHandleValue)
            {
                return nullptr;
            }

            const auto instanceIt = m_instances.find(a_instanceHandle.value);
            return instanceIt != m_instances.end()
                ? &instanceIt->second
                : nullptr;
        }

        static void destroy_instance_record(InstanceRecord& a_record) noexcept
        {
            if (a_record.scriptClass != nullptr &&
                a_record.scriptClass->destroyState != nullptr)
            {
                a_record.scriptClass->destroyState(a_record.state);
            }

            a_record.scriptClass = nullptr;
            a_record.state = nullptr;
        }

        std::unordered_map<uint64_t, InstanceRecord> m_instances{};
        uint64_t m_nextInstanceId = 1;
        const CueEngineApi* m_engineApi = nullptr;
    };

    [[nodiscard]] constexpr CueStringView make_script_string_view(
        const char* a_text,
        uint32_t a_size) noexcept
    {
        return CueStringView{ a_text, a_size };
    }

    [[nodiscard]] constexpr uint64_t hash_script_schema(
        std::string_view a_text) noexcept
    {
        uint64_t hash = 14695981039346656037ull;
        for (char ch : a_text)
        {
            hash ^= static_cast<uint8_t>(ch);
            hash *= 1099511628211ull;
        }

        return hash;
    }

    [[nodiscard]] inline bool string_view_equals(
        CueStringView a_left,
        CueStringView a_right) noexcept
    {
        if (a_left.data == nullptr || a_right.data == nullptr)
        {
            return false;
        }
        if (a_left.size != a_right.size)
        {
            return false;
        }

        return std::memcmp(a_left.data, a_right.data, a_left.size) == 0;
    }
}

#define CUE_SCRIPT_STRING_VIEW(a_literal) \
    ::Cue::Core::Native::make_script_string_view( \
        (a_literal), static_cast<uint32_t>(sizeof(a_literal) - 1u))

#define CUE_FIELD_FLOAT_META(a_groupNameLiteral, a_nameLiteral, a_defaultValue, a_flags) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_Float, \
        (a_defaultValue), \
        0, \
        0, \
        0, \
        0, \
        0, \
        CueEntityHandle{ k_cueInvalidHandleValue }, \
        CueStringView{ nullptr, 0 }, \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_None, \
        static_cast<CueScriptFieldFlags>(a_flags) \
    }

#define CUE_FIELD_FLOAT(a_nameLiteral, a_defaultValue) \
    CUE_FIELD_FLOAT_META( \
        "", \
        a_nameLiteral, \
        a_defaultValue, \
        CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize)

#define CUE_FIELD_INT32_META(a_groupNameLiteral, a_nameLiteral, a_defaultValue, a_flags) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_Int32, \
        0.0f, \
        (a_defaultValue), \
        0, \
        0, \
        0, \
        0, \
        CueEntityHandle{ k_cueInvalidHandleValue }, \
        CueStringView{ nullptr, 0 }, \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_None, \
        static_cast<CueScriptFieldFlags>(a_flags) \
    }

#define CUE_FIELD_INT32(a_nameLiteral, a_defaultValue) \
    CUE_FIELD_INT32_META( \
        "", \
        a_nameLiteral, \
        a_defaultValue, \
        CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize)

#define CUE_FIELD_BOOL_META(a_groupNameLiteral, a_nameLiteral, a_defaultValue, a_flags) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_Bool, \
        0.0f, \
        0, \
        static_cast<uint8_t>((a_defaultValue) ? 1 : 0), \
        0, \
        0, \
        0, \
        CueEntityHandle{ k_cueInvalidHandleValue }, \
        CueStringView{ nullptr, 0 }, \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_None, \
        static_cast<CueScriptFieldFlags>(a_flags) \
    }

#define CUE_FIELD_BOOL(a_nameLiteral, a_defaultValue) \
    CUE_FIELD_BOOL_META( \
        "", \
        a_nameLiteral, \
        a_defaultValue, \
        CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize)

#define CUE_FIELD_ENTITY_META(a_groupNameLiteral, a_nameLiteral, a_defaultValue, a_flags) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_EntityRef, \
        0.0f, \
        0, \
        0, \
        0, \
        0, \
        0, \
        (a_defaultValue), \
        CueStringView{ nullptr, 0 }, \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_None, \
        static_cast<CueScriptFieldFlags>(a_flags) \
    }

#define CUE_FIELD_ENTITY(a_nameLiteral, a_defaultValue) \
    CUE_FIELD_ENTITY_META( \
        "", \
        a_nameLiteral, \
        a_defaultValue, \
        CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize)

#define CUE_FIELD_SCRIPT_CLASS_META(a_groupNameLiteral, a_nameLiteral, a_defaultClassLiteral, a_flags) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_ClassRef, \
        0.0f, \
        0, \
        0, \
        0, \
        0, \
        0, \
        CueEntityHandle{ k_cueInvalidHandleValue }, \
        CUE_SCRIPT_STRING_VIEW(a_defaultClassLiteral), \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_None, \
        static_cast<CueScriptFieldFlags>(a_flags) \
    }

#define CUE_FIELD_SCRIPT_CLASS(a_nameLiteral, a_defaultClassLiteral) \
    CUE_FIELD_SCRIPT_CLASS_META( \
        "", \
        a_nameLiteral, \
        a_defaultClassLiteral, \
        CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize)

#define CUE_FIELD_SCRIPT_REF_ENTITY(a_groupNameLiteral, a_nameLiteral, a_defaultValue) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_EntityRef, \
        0.0f, \
        0, \
        0, \
        0, \
        0, \
        0, \
        (a_defaultValue), \
        CueStringView{ nullptr, 0 }, \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_ScriptReferenceEntity, \
        static_cast<CueScriptFieldFlags>( \
            CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize) \
    }

#define CUE_FIELD_SCRIPT_REF_CLASS(a_groupNameLiteral, a_nameLiteral, a_defaultClassLiteral) \
    CueScriptFieldValue{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        CueScriptFieldType_ClassRef, \
        0.0f, \
        0, \
        0, \
        0, \
        0, \
        0, \
        CueEntityHandle{ k_cueInvalidHandleValue }, \
        CUE_SCRIPT_STRING_VIEW(a_defaultClassLiteral), \
        CUE_SCRIPT_STRING_VIEW(a_groupNameLiteral), \
        CueScriptFieldReferenceRole_ScriptReferenceClass, \
        static_cast<CueScriptFieldFlags>( \
            CueScriptFieldFlag_EditAnywhere | CueScriptFieldFlag_Serialize) \
    }

#define CUE_FUNCTION_META(a_nameLiteral, a_flags, a_invokeFn) \
    ::Cue::Core::Native::ScriptFunctionBinding{ \
        CueScriptFunctionDefinition{ \
            CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
            static_cast<CueScriptFunctionFlags>(a_flags) \
        }, \
        (a_invokeFn) \
    }

#define CUE_FUNCTION(a_nameLiteral, a_invokeFn) \
    CUE_FUNCTION_META(a_nameLiteral, CueScriptFunctionFlag_None, a_invokeFn)

#define CUE_SCRIPT(a_nameLiteral, a_stateVersion, a_stateType, a_schemaLiteral, \
    a_fields, a_functions, a_createState, a_destroyState, a_updateState, a_serializeState, a_restoreState) \
    ::Cue::Core::Native::ScriptClassDefinition{ \
        CUE_SCRIPT_STRING_VIEW(a_nameLiteral), \
        (a_fields).data(), \
        static_cast<uint32_t>((a_fields).size()), \
        (a_functions).data(), \
        static_cast<uint32_t>((a_functions).size()), \
        CueScriptStateDescriptor{ \
            static_cast<uint32_t>(a_stateVersion), \
            static_cast<uint32_t>(sizeof(a_stateType)), \
            ::Cue::Core::Native::hash_script_schema(a_schemaLiteral) \
        }, \
        (a_createState), \
        (a_destroyState), \
        (a_updateState), \
        (a_serializeState), \
        (a_restoreState) \
    }
