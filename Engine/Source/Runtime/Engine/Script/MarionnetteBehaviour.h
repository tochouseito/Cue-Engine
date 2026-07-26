#pragma once

/// **********************************************************************
/// GameScript の状態と Runtime World を ABI 経由で結び付ける
/// **********************************************************************

// === Native includes ===
#include <Native/ScriptModuleRuntime.h>

// === C++ includes ===
#include <new>
#include <string_view>
#include <type_traits>

namespace Cue::Script
{
    /// @brief GameScript が値として操作するローカル Transform
    using Transform = Core::Native::ScriptTransform;

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

    private:
        template <typename T>
        friend Core::Native::ScriptClassDefinition
        make_marionnette_script_class_definition(std::string_view a_className) noexcept;

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

        return Core::Native::ScriptClassDefinition{
            Core::Native::ScriptStringView{
                a_className.data(), static_cast<uint32_t>(a_className.size())},
            [](const Core::Native::ScriptEngineApi* a_engineApi,
               const Core::Native::ScriptCreateInfo* a_createInfo,
               void** a_outState) noexcept -> Core::Native::ScriptAbiResult
            {
                if (a_engineApi == nullptr || a_createInfo == nullptr || a_outState == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InvalidArgument;
                }

                T* state = new (std::nothrow) T(a_engineApi, a_createInfo->entity);
                if (state == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InternalError;
                }

                *a_outState = state;
                return Core::Native::ScriptAbiResult::Ok;
            },
            [](void* a_state) noexcept
            {
                auto* state = static_cast<T*>(a_state);
                if (state != nullptr)
                {
                    state->on_destroy();
                    delete state;
                }
            },
            [](void* a_state) noexcept -> Core::Native::ScriptAbiResult
            {
                if (a_state == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InvalidArgument;
                }

                auto* state = static_cast<T*>(a_state);
                Core::Native::ScriptAbiResult result = state->begin_lifecycle(0.0f);
                if (result != Core::Native::ScriptAbiResult::Ok)
                {
                    return result;
                }

                state->start();
                return state->end_lifecycle();
            },
            [](void* a_state, float a_deltaTimeSeconds) noexcept -> Core::Native::ScriptAbiResult
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
            }};
    }
} // namespace Cue::Script
