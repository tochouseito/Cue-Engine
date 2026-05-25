#include "NavigationGeometryCollector.h"

// === C++ includes ===
#include <cmath>
#include <cstddef>
#include <cstring>

namespace Cue::GameCore
{
    namespace
    {
        inline constexpr uint64_t k_fnvOffset = 14695981039346656037ull;
        inline constexpr uint64_t k_fnvPrime = 1099511628211ull;

        void hash_bytes(uint64_t& a_hash, const void* a_data, size_t a_size) noexcept
        {
            const auto* bytes = static_cast<const unsigned char*>(a_data);
            for (size_t byteIndex = 0; byteIndex < a_size; ++byteIndex)
            {
                a_hash ^= bytes[byteIndex];
                a_hash *= k_fnvPrime;
            }
        }

        void hash_triangle(uint64_t& a_hash,
            const NavMeshTriangle& a_triangle) noexcept
        {
            hash_bytes(a_hash, &a_triangle.v0, sizeof(a_triangle.v0));
            hash_bytes(a_hash, &a_triangle.v1, sizeof(a_triangle.v1));
            hash_bytes(a_hash, &a_triangle.v2, sizeof(a_triangle.v2));
            hash_bytes(a_hash, &a_triangle.area, sizeof(a_triangle.area));
        }

        [[nodiscard]] Math::float3 transform_point(
            const Math::float4x4& a_matrix,
            const Math::float4& a_point) noexcept
        {
            const float x =
                a_point.x * a_matrix.values[0][0] +
                a_point.y * a_matrix.values[1][0] +
                a_point.z * a_matrix.values[2][0] +
                a_point.w * a_matrix.values[3][0];
            const float y =
                a_point.x * a_matrix.values[0][1] +
                a_point.y * a_matrix.values[1][1] +
                a_point.z * a_matrix.values[2][1] +
                a_point.w * a_matrix.values[3][1];
            const float z =
                a_point.x * a_matrix.values[0][2] +
                a_point.y * a_matrix.values[1][2] +
                a_point.z * a_matrix.values[2][2] +
                a_point.w * a_matrix.values[3][2];
            const float w =
                a_point.x * a_matrix.values[0][3] +
                a_point.y * a_matrix.values[1][3] +
                a_point.z * a_matrix.values[2][3] +
                a_point.w * a_matrix.values[3][3];

            if (std::abs(w) > 0.000001f && std::abs(w - 1.0f) > 0.000001f)
            {
                return Math::float3(x / w, y / w, z / w);
            }

            return Math::float3(x, y, z);
        }

        [[nodiscard]] uint8_t resolve_area(ECS::ECSManager& a_ecs,
            ECS::Entity a_entity) noexcept
        {
            const ECS::NavMeshBakeSourceComponent* source =
                a_ecs.get_component<ECS::NavMeshBakeSourceComponent>(a_entity);
            if (source == nullptr)
            {
                return static_cast<uint8_t>(NavAreaType::Walkable);
            }

            return source->area;
        }

        [[nodiscard]] bool is_bake_included(ECS::ECSManager& a_ecs,
            ECS::Entity a_entity) noexcept
        {
            const ECS::NavMeshBakeSourceComponent* source =
                a_ecs.get_component<ECS::NavMeshBakeSourceComponent>(a_entity);
            return source == nullptr || source->isIncluded;
        }
    }

    Result NavigationGeometryCollector::append_model(
        const Core::Native::ModelData& a_modelData,
        const ECS::TransformComponent& a_transform,
        uint8_t a_area,
        NavMeshBuildInput& a_outInput) noexcept
    {
        const Math::float4x4 worldMatrix = Math::make_affine_matrix(
            a_transform.scale,
            a_transform.rotation,
            a_transform.position);

        uint64_t hash = a_outInput.sourceGeometryHash == 0
            ? k_fnvOffset
            : a_outInput.sourceGeometryHash;

        auto append_mesh =
            [&a_outInput, &hash, a_area](
                const Core::Native::MeshData& a_mesh,
                const Math::float4x4& a_matrix) -> Result
        {
            if (a_mesh.indices.empty() || a_mesh.positions.empty())
            {
                return Result::ok();
            }

            if ((a_mesh.indices.size() % 3u) != 0u)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Navigation source mesh index count must be a multiple of 3.");
            }

            a_outInput.triangles.reserve(
                a_outInput.triangles.size() + a_mesh.indices.size() / 3u);

            for (size_t index = 0; index < a_mesh.indices.size(); index += 3u)
            {
                const uint32_t i0 = a_mesh.indices[index];
                const uint32_t i1 = a_mesh.indices[index + 1u];
                const uint32_t i2 = a_mesh.indices[index + 2u];
                if (i0 >= a_mesh.positions.size() ||
                    i1 >= a_mesh.positions.size() ||
                    i2 >= a_mesh.positions.size())
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Navigation source mesh index is out of range.");
                }

                NavMeshTriangle triangle{};
                triangle.v0 = transform_point(a_matrix, a_mesh.positions[i0]);
                triangle.v1 = transform_point(a_matrix, a_mesh.positions[i1]);
                triangle.v2 = transform_point(a_matrix, a_mesh.positions[i2]);
                triangle.area = a_area;
                hash_triangle(hash, triangle);
                a_outInput.triangles.push_back(triangle);
            }

            return Result::ok();
        };

        if (a_modelData.renderParts.empty())
        {
            for (const Core::Native::MeshData& mesh : a_modelData.meshes)
            {
                Result result = append_mesh(mesh, worldMatrix);
                if (!result)
                {
                    return result;
                }
            }
        }
        else
        {
            for (const Core::Native::ModelRenderPartData& renderPart :
                a_modelData.renderParts)
            {
                if (renderPart.meshIndex >= a_modelData.meshes.size())
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Navigation source render part mesh index is out of range.");
                }

                Result result = append_mesh(
                    a_modelData.meshes[renderPart.meshIndex],
                    renderPart.localTransform * worldMatrix);
                if (!result)
                {
                    return result;
                }
            }
        }

        a_outInput.sourceGeometryHash =
            a_outInput.triangles.empty() ? 0 : hash;
        return Result::ok();
    }

    Result NavigationGeometryCollector::append_model(
        AssetManager& a_assetManager,
        std::string_view a_modelName,
        const ECS::TransformComponent& a_transform,
        uint8_t a_area,
        NavMeshBuildInput& a_outInput) noexcept
    {
        if (a_modelName.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Navigation source model name must not be empty.");
        }

        ModelHandle modelHandle{};
        Result result = a_assetManager.get_model(a_modelName, modelHandle);
        if (!result)
        {
            return result;
        }

        Core::Native::ModelData modelData{};
        result = a_assetManager.get_model(modelHandle, modelData);
        if (!result)
        {
            return result;
        }

        return append_model(modelData, a_transform, a_area, a_outInput);
    }

    Result NavigationGeometryCollector::append_entity(
        ECS::ECSManager& a_ecs,
        AssetManager& a_assetManager,
        ECS::Entity a_entity,
        NavMeshBuildInput& a_outInput) noexcept
    {
        if (!is_bake_included(a_ecs, a_entity))
        {
            return Result::ok();
        }

        const ECS::TransformComponent* transform =
            a_ecs.get_component<ECS::TransformComponent>(a_entity);
        const ECS::MeshFilterComponent* meshFilter =
            a_ecs.get_component<ECS::MeshFilterComponent>(a_entity);
        if (transform == nullptr || meshFilter == nullptr)
        {
            return Result::ok();
        }

        std::string modelName = meshFilter->modelName;
        if (modelName.empty() && meshFilter->meshId != ECS::k_invalidMeshId)
        {
            (void)a_assetManager.get_model_name_from_mesh_id(
                meshFilter->meshId, modelName);
        }

        if (modelName.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Navigation source entity does not reference a model.");
        }

        return append_model(a_assetManager, modelName, *transform,
            resolve_area(a_ecs, a_entity), a_outInput);
    }

    Result NavigationGeometryCollector::collect_entities(
        ECS::ECSManager& a_ecs,
        AssetManager& a_assetManager,
        std::span<const ECS::Entity> a_entities,
        NavMeshBuildInput& a_outInput) noexcept
    {
        a_outInput = {};
        for (ECS::Entity entity : a_entities)
        {
            Result result =
                append_entity(a_ecs, a_assetManager, entity, a_outInput);
            if (!result)
            {
                return result;
            }
        }

        return a_outInput.triangles.empty()
            ? Result::fail(Code::InvalidArgument, Severity::Error,
                "Navigation source geometry was empty.")
            : Result::ok();
    }
}
