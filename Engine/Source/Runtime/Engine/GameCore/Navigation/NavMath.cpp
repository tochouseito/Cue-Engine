// === Engine includes ===
#include "GameCore/Navigation/NavMath.h"

// === C++ includes ===
#include <algorithm>
#include <cmath>

namespace Cue::GameCore::NavMath
{
    namespace
    {
        [[nodiscard]] bool is_near_zero(float a_value) noexcept
        {
            return std::fabs(a_value) <= k_epsilon;
        }

        [[nodiscard]] Math::float3 closest_point_on_degenerate_triangle(
            const Math::float3& a_point,
            const Math::float3& a_a,
            const Math::float3& a_b,
            const Math::float3& a_c) noexcept
        {
            const Math::float3 ab =
                closest_point_on_segment(a_point, a_a, a_b);
            const Math::float3 bc =
                closest_point_on_segment(a_point, a_b, a_c);
            const Math::float3 ca =
                closest_point_on_segment(a_point, a_c, a_a);

            Math::float3 best = ab;
            float bestDistance = distance_sq(a_point, ab);

            const float bcDistance = distance_sq(a_point, bc);
            if (bcDistance < bestDistance)
            {
                best = bc;
                bestDistance = bcDistance;
            }

            const float caDistance = distance_sq(a_point, ca);
            if (caDistance < bestDistance)
            {
                best = ca;
            }

            return best;
        }
    }

    Math::float2 project_xz(const Math::float3& a_position) noexcept
    {
        return Math::float2(a_position.x, a_position.z);
    }

    float cross_2d(
        const Math::float2& a_left,
        const Math::float2& a_right) noexcept
    {
        return a_left.x * a_right.y - a_left.y * a_right.x;
    }

    float distance_sq(
        const Math::float3& a_left,
        const Math::float3& a_right) noexcept
    {
        return (a_left - a_right).length_sq();
    }

    float distance(
        const Math::float3& a_left,
        const Math::float3& a_right) noexcept
    {
        return std::sqrt(distance_sq(a_left, a_right));
    }

    bool point_in_triangle_xz(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c,
        float a_epsilon) noexcept
    {
        const Math::float2 point = project_xz(a_point);
        const Math::float2 a = project_xz(a_a);
        const Math::float2 b = project_xz(a_b);
        const Math::float2 c = project_xz(a_c);

        const float d1 = cross_2d(b - a, point - a);
        const float d2 = cross_2d(c - b, point - b);
        const float d3 = cross_2d(a - c, point - c);

        const bool hasNegative =
            d1 < -a_epsilon || d2 < -a_epsilon || d3 < -a_epsilon;
        const bool hasPositive =
            d1 > a_epsilon || d2 > a_epsilon || d3 > a_epsilon;

        return !(hasNegative && hasPositive);
    }

    bool triangle_height_at_xz(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c,
        float& a_outHeight) noexcept
    {
        const Math::float2 point = project_xz(a_point);
        const Math::float2 a = project_xz(a_a);
        const Math::float2 b = project_xz(a_b);
        const Math::float2 c = project_xz(a_c);

        const Math::float2 v0 = b - a;
        const Math::float2 v1 = c - a;
        const Math::float2 v2 = point - a;
        const float denom = cross_2d(v0, v1);

        if (is_near_zero(denom))
        {
            return false;
        }

        const float invDenom = 1.0f / denom;
        const float bWeight = cross_2d(v2, v1) * invDenom;
        const float cWeight = cross_2d(v0, v2) * invDenom;
        const float aWeight = 1.0f - bWeight - cWeight;

        a_outHeight =
            a_a.y * aWeight + a_b.y * bWeight + a_c.y * cWeight;
        return true;
    }

    Math::float3 closest_point_on_segment(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b) noexcept
    {
        const Math::float3 ab = a_b - a_a;
        const float denom = ab.length_sq();

        if (is_near_zero(denom))
        {
            return a_a;
        }

        const float t =
            std::clamp((a_point - a_a).dot(ab) / denom, 0.0f, 1.0f);
        return a_a + ab * t;
    }

    Math::float3 closest_point_on_triangle(
        const Math::float3& a_point,
        const Math::float3& a_a,
        const Math::float3& a_b,
        const Math::float3& a_c) noexcept
    {
        const Math::float3 ab = a_b - a_a;
        const Math::float3 ac = a_c - a_a;
        const Math::float3 normal = ab.cross(ac);

        if (is_near_zero(normal.length_sq()))
        {
            return closest_point_on_degenerate_triangle(
                a_point,
                a_a,
                a_b,
                a_c);
        }

        const Math::float3 ap = a_point - a_a;
        const float d1 = ab.dot(ap);
        const float d2 = ac.dot(ap);
        if (d1 <= 0.0f && d2 <= 0.0f)
        {
            return a_a;
        }

        const Math::float3 bp = a_point - a_b;
        const float d3 = ab.dot(bp);
        const float d4 = ac.dot(bp);
        if (d3 >= 0.0f && d4 <= d3)
        {
            return a_b;
        }

        const float vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        {
            const float v = d1 / (d1 - d3);
            return a_a + ab * v;
        }

        const Math::float3 cp = a_point - a_c;
        const float d5 = ab.dot(cp);
        const float d6 = ac.dot(cp);
        if (d6 >= 0.0f && d5 <= d6)
        {
            return a_c;
        }

        const float vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        {
            const float w = d2 / (d2 - d6);
            return a_a + ac * w;
        }

        const float va = d3 * d6 - d5 * d4;
        if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        {
            const float w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return a_b + (a_c - a_b) * w;
        }

        const float denom = 1.0f / (va + vb + vc);
        const float v = vb * denom;
        const float w = vc * denom;
        return a_a + ab * v + ac * w;
    }
}
