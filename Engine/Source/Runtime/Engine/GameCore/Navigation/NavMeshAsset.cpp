// === Engine includes ===
#include "GameCore/Navigation/NavMeshAsset.h"

namespace Cue::GameCore
{
    void NavMeshAsset::clear() noexcept
    {
        vertices.clear();
        polys.clear();
    }

    bool NavMeshAsset::is_valid_poly(NavPolyId a_polyId) const noexcept
    {
        return a_polyId < polys.size();
    }

    void NavMeshAsset::compute_poly_centers() noexcept
    {
        constexpr float k_triangleVertexCount = 3.0f;

        for (NavPoly& poly : polys)
        {
            const bool hasValidIndices =
                poly.indices[0] < vertices.size() &&
                poly.indices[1] < vertices.size() &&
                poly.indices[2] < vertices.size();

            if (!hasValidIndices)
            {
                poly.center = Math::float3::zero();
                continue;
            }

            const Math::float3& a = vertices[poly.indices[0]].position;
            const Math::float3& b = vertices[poly.indices[1]].position;
            const Math::float3& c = vertices[poly.indices[2]].position;

            poly.center = (a + b + c) / k_triangleVertexCount;
        }
    }
}
