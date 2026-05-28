// FirstPersonCameraControllerSystem の役割と公開要素を定義する

#pragma once

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>

// === PAL includes ===
#include <Input/InputManager.h>

// === C++ includes ===
#include <algorithm>

namespace Cue::ECS
{
    class FirstPersonCameraControllerSystem final
        : public ECSManager::System<TransformComponent,
              CameraComponent,
              FirstPersonCameraControllerComponent>
    {
    public:
        explicit FirstPersonCameraControllerSystem(
            PAL::InputManager* a_inputManager)
            : ECSManager::System<TransformComponent,
                  CameraComponent,
                  FirstPersonCameraControllerComponent>(
                  [this](Entity a_entity,
                      TransformComponent& a_transform,
                      CameraComponent& a_camera,
                      FirstPersonCameraControllerComponent& a_controller,
                      const UpdateContext& a_context)
                  {
                      update_component(a_entity, a_transform, a_camera,
                          a_controller, a_context);
                  })
            , m_inputManager(a_inputManager)
        {}

    private:
        void update_component(Entity,
            TransformComponent& a_transform,
            CameraComponent& a_camera,
            FirstPersonCameraControllerComponent& a_controller,
            const UpdateContext&)
        {
            if (!a_controller.isEnabled)
            {
                return;
            }

            if (m_inputManager != nullptr)
            {
                const PAL::MouseDelta mouseDelta = m_inputManager->mouse_delta();
                a_controller.yaw +=
                    static_cast<float>(mouseDelta.x) *
                    a_controller.mouseSensitivity;
                a_controller.pitch +=
                    static_cast<float>(mouseDelta.y) *
                    a_controller.mouseSensitivity;
                a_controller.pitch = std::clamp(a_controller.pitch,
                    a_controller.minPitch, a_controller.maxPitch);
            }

            if (a_controller.followsTarget &&
                a_controller.targetEntity != GameCore::k_invalidEntityId &&
                m_pEcs != nullptr)
            {
                TransformComponent* targetTransform =
                    m_pEcs->get_component<TransformComponent>(
                        a_controller.targetEntity);
                if (targetTransform != nullptr)
                {
                    a_transform.position =
                        targetTransform->position + a_controller.offset;
                    if (a_controller.rotatesTargetYaw)
                    {
                        Math::float3 targetRotation =
                            Math::quaternion_to_euler_xyz(
                                targetTransform->rotation);
                        targetRotation.y = a_controller.yaw;
                        targetTransform->rotation =
                            Math::quaternion_from_euler_xyz(
                                targetRotation);
                    }
                }
            }

            Math::float3 cameraRotation =
                Math::quaternion_to_euler_xyz(a_transform.rotation);
            cameraRotation.x = a_controller.pitch;
            cameraRotation.y = a_controller.yaw;
            a_transform.rotation = Math::quaternion_from_euler_xyz(
                cameraRotation);
            a_camera.fovY = a_controller.fovY;
        }

        PAL::InputManager* m_inputManager = nullptr;
    };
}
