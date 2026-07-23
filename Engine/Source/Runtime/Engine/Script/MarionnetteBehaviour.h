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
        /// @brief アタッチ先 Entity のローカル Transform を取得する
        [[nodiscard]] Core::Native::ScriptAbiResult get_transform(
            Core::Native::ScriptTransformQuaternion& a_outTransform) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->getTransformQuaternion == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return m_engineApi->getTransformQuaternion(
                m_engineApi->userData, m_entity, &a_outTransform);
        }

        /// @brief アタッチ先 Entity のローカル Transform を更新する
        [[nodiscard]] Core::Native::ScriptAbiResult set_transform(
            const Core::Native::ScriptTransformQuaternion& a_transform) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->setTransformQuaternion == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return m_engineApi->setTransformQuaternion(
                m_engineApi->userData, m_entity, &a_transform);
        }

        /// @brief アタッチ先 Entity のローカル Transform を XYZ 順の Euler 回転で取得する
        /// @param a_outTransform 回転をラジアンで受け取る Transform
        [[nodiscard]] Core::Native::ScriptAbiResult get_transform(
            Core::Native::ScriptTransformEuler& a_outTransform) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->getTransformEuler == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return m_engineApi->getTransformEuler(
                m_engineApi->userData, m_entity, &a_outTransform);
        }

        /// @brief アタッチ先 Entity のローカル Transform を XYZ 順の Euler 回転で更新する
        /// @param a_transform 回転をラジアンで指定する Transform
        [[nodiscard]] Core::Native::ScriptAbiResult set_transform(
            const Core::Native::ScriptTransformEuler& a_transform) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->setTransformEuler == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return m_engineApi->setTransformEuler(
                m_engineApi->userData, m_entity, &a_transform);
        }

        /// @brief XYZ 順の Euler 差分回転を現在のローカル回転へ合成する
        /// @param a_rotationRadians 各軸へ加えるラジアン回転
        [[nodiscard]] Core::Native::ScriptAbiResult rotate(
            const Core::Native::ScriptVector3& a_rotationRadians) const noexcept
        {
            if (m_engineApi == nullptr || m_engineApi->rotateTransformEuler == nullptr)
            {
                return Core::Native::ScriptAbiResult::InvalidState;
            }

            return m_engineApi->rotateTransformEuler(
                m_engineApi->userData, m_entity, &a_rotationRadians);
        }

    private:
        const Core::Native::ScriptEngineApi* m_engineApi = nullptr;
        Core::Native::ScriptEntityHandle m_entity{};
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

                static_cast<T*>(a_state)->start();
                return Core::Native::ScriptAbiResult::Ok;
            },
            [](void* a_state, float a_deltaTimeSeconds) noexcept -> Core::Native::ScriptAbiResult
            {
                if (a_state == nullptr)
                {
                    return Core::Native::ScriptAbiResult::InvalidArgument;
                }

                static_cast<T*>(a_state)->update(a_deltaTimeSeconds);
                return Core::Native::ScriptAbiResult::Ok;
            }};
    }
} // namespace Cue::Script
