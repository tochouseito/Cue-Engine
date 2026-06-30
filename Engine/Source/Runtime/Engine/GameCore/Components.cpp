#include "Components.h"

namespace Cue::ECS
{
    RenderableInfoComponent::RenderableInfoComponent() = default;

    RenderableInfoComponent::RenderableInfoComponent(const RenderableInfoComponent&) = default;

    RenderableInfoComponent& RenderableInfoComponent::operator=(const RenderableInfoComponent&) = default;

    RenderableInfoComponent::RenderableInfoComponent(RenderableInfoComponent&&) = default;

    RenderableInfoComponent& RenderableInfoComponent::operator=(RenderableInfoComponent&&) = default;

    TransformComponent::TransformComponent() = default;

    TransformComponent::TransformComponent(const TransformComponent&) = default;

    TransformComponent& TransformComponent::operator=(const TransformComponent&) = default;

    TransformComponent::TransformComponent(TransformComponent&&) = default;

    TransformComponent& TransformComponent::operator=(TransformComponent&&) = default;

    WorldTransformComponent::WorldTransformComponent() = default;

    WorldTransformComponent::WorldTransformComponent(const WorldTransformComponent&) = default;

    WorldTransformComponent& WorldTransformComponent::operator=(const WorldTransformComponent&) = default;

    WorldTransformComponent::WorldTransformComponent(WorldTransformComponent&&) = default;

    WorldTransformComponent& WorldTransformComponent::operator=(WorldTransformComponent&&) = default;

    CameraComponent::CameraComponent() = default;

    CameraComponent::CameraComponent(const CameraComponent&) = default;

    CameraComponent& CameraComponent::operator=(const CameraComponent&) = default;

    CameraComponent::CameraComponent(CameraComponent&&) = default;

    CameraComponent& CameraComponent::operator=(CameraComponent&&) = default;

    MeshFilterComponent::MeshFilterComponent() = default;

    MeshFilterComponent::MeshFilterComponent(const MeshFilterComponent&) = default;

    MeshFilterComponent& MeshFilterComponent::operator=(const MeshFilterComponent&) = default;

    MeshFilterComponent::MeshFilterComponent(MeshFilterComponent&&) = default;

    MeshFilterComponent& MeshFilterComponent::operator=(MeshFilterComponent&&) = default;

    StaticMeshRendererComponent::StaticMeshRendererComponent() = default;

    StaticMeshRendererComponent::StaticMeshRendererComponent(const StaticMeshRendererComponent&) = default;

    StaticMeshRendererComponent& StaticMeshRendererComponent::operator=(const StaticMeshRendererComponent&) = default;

    StaticMeshRendererComponent::StaticMeshRendererComponent(StaticMeshRendererComponent&&) = default;

    StaticMeshRendererComponent& StaticMeshRendererComponent::operator=(StaticMeshRendererComponent&&) = default;
} // namespace Cue::ECS
