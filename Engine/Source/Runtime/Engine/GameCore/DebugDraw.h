#pragma once

// === Math includes ===
#include <CueMath.h>

// === C++ includes ===
#include <algorithm>
#include <vector>

namespace Cue::GameCore
{
    enum class DebugDrawPrimitiveType : uint8_t
    {
        Line,
        Sphere,
        Box,
    };

    struct DebugDrawPrimitive final
    {
        DebugDrawPrimitiveType type = DebugDrawPrimitiveType::Line;
        Math::float3 start = Math::float3::zero();
        Math::float3 end = Math::float3::zero();
        Math::float3 center = Math::float3::zero();
        Math::float3 halfExtent = Math::float3(0.5f, 0.5f, 0.5f);
        Math::float4 color = Math::float4(1.0f, 1.0f, 1.0f, 1.0f);
        float radius = 0.5f;
        float remainingSeconds = 0.0f;
    };

    class DebugDrawBuffer final
    {
    public:
        void clear() noexcept
        {
            m_primitives.clear();
        }

        void update(float a_deltaTime) noexcept
        {
            if (a_deltaTime <= 0.0f)
            {
                return;
            }

            for (DebugDrawPrimitive& primitive : m_primitives)
            {
                if (primitive.remainingSeconds > 0.0f)
                {
                    primitive.remainingSeconds -= a_deltaTime;
                }
            }

            std::erase_if(m_primitives,
                [](const DebugDrawPrimitive& a_primitive)
                {
                    return a_primitive.remainingSeconds < 0.0f;
                });
        }

        void add_line(const Math::float3& a_start,
            const Math::float3& a_end,
            const Math::float4& a_color,
            float a_durationSeconds)
        {
            DebugDrawPrimitive primitive{};
            primitive.type = DebugDrawPrimitiveType::Line;
            primitive.start = a_start;
            primitive.end = a_end;
            primitive.color = a_color;
            primitive.remainingSeconds = a_durationSeconds;
            m_primitives.push_back(primitive);
        }

        void add_sphere(const Math::float3& a_center,
            float a_radius,
            const Math::float4& a_color,
            float a_durationSeconds)
        {
            DebugDrawPrimitive primitive{};
            primitive.type = DebugDrawPrimitiveType::Sphere;
            primitive.center = a_center;
            primitive.radius = a_radius;
            primitive.color = a_color;
            primitive.remainingSeconds = a_durationSeconds;
            m_primitives.push_back(primitive);
        }

        void add_box(const Math::float3& a_center,
            const Math::float3& a_halfExtent,
            const Math::float4& a_color,
            float a_durationSeconds)
        {
            DebugDrawPrimitive primitive{};
            primitive.type = DebugDrawPrimitiveType::Box;
            primitive.center = a_center;
            primitive.halfExtent = a_halfExtent;
            primitive.color = a_color;
            primitive.remainingSeconds = a_durationSeconds;
            m_primitives.push_back(primitive);
        }

        [[nodiscard]] const std::vector<DebugDrawPrimitive>& primitives()
            const noexcept
        {
            return m_primitives;
        }

    private:
        std::vector<DebugDrawPrimitive> m_primitives{};
    };
}
