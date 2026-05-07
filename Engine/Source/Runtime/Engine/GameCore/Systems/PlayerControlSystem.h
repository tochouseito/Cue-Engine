#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === PAL includes ===
#include <Input/InputManager.h>

// === C++ includes ===
#include <cmath>

namespace Cue::ECS
{
    class PlayerControlSystem final
        : public ECSManager::System<GameCore::BaseComponent,
              TransformComponent,
              CharacterControllerComponent>
    {
    public:
        explicit PlayerControlSystem(
            PAL::InputManager* a_inputManager) noexcept
            : ECSManager::System<GameCore::BaseComponent,
                  TransformComponent,
                  CharacterControllerComponent>(
                  [this](Entity,
                      const GameCore::BaseComponent& a_base,
                      TransformComponent& a_transform,
                      CharacterControllerComponent& a_controller,
                      const UpdateContext&)
                  {
                      update_component(a_base, a_transform, a_controller);
                  })
            , m_inputManager(a_inputManager)
        {}

    private:
        void update_component(const GameCore::BaseComponent& a_base,
            const TransformComponent& a_transform,
            CharacterControllerComponent& a_controller) const noexcept
        {
            if (m_inputManager == nullptr || !a_base.isActiveSelf ||
                a_base.tag != "Player")
            {
                return;
            }

            Math::float3 localInput = Math::float3::zero();
            if (m_inputManager->push_key(PAL::Key::W))
            {
                localInput.z += 1.0f;
            }
            if (m_inputManager->push_key(PAL::Key::S))
            {
                localInput.z -= 1.0f;
            }
            if (m_inputManager->push_key(PAL::Key::A))
            {
                localInput.x -= 1.0f;
            }
            if (m_inputManager->push_key(PAL::Key::D))
            {
                localInput.x += 1.0f;
            }

            const float lengthSquared =
                localInput.x * localInput.x + localInput.z * localInput.z;
            if (lengthSquared <= 0.0f)
            {
                a_controller.moveVelocity = Math::float3::zero();
                return;
            }

            const float scale =
                a_controller.maxSpeed / std::sqrt(lengthSquared);
            localInput.x *= scale;
            localInput.z *= scale;

            const float yaw = a_transform.rotation.y;
            const float sinYaw = std::sin(yaw);
            const float cosYaw = std::cos(yaw);

            const Math::float3 right(cosYaw, 0.0f, -sinYaw);
            const Math::float3 forward(sinYaw, 0.0f, cosYaw);
            a_controller.moveVelocity =
                right * localInput.x + forward * localInput.z;
        }

        PAL::InputManager* m_inputManager = nullptr;
    };
}
