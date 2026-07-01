#include "DrawScene.h"

// === C++ includes ===
#include <new>

namespace Cue::DrawSystem
{
    DrawScene::DrawScene() = default;

    DrawScene::~DrawScene() = default;

    DrawScene::DrawScene(const DrawScene&) = default;

    DrawScene& DrawScene::operator=(const DrawScene&) = default;

    DrawScene::DrawScene(DrawScene&&) noexcept = default;

    DrawScene& DrawScene::operator=(DrawScene&&) noexcept = default;

    void DrawScene::clear() noexcept
    {
        m_staticMeshObjects.clear();
        m_cameras.clear();
        m_renderableInfos.clear();
        m_transforms.clear();
    }

    size_t DrawScene::object_count() const noexcept
    {
        return m_staticMeshObjects.size();
    }

    Result DrawScene::add_static_mesh_object(const StaticMeshDrawObject& a_object,
        const GpuData::RenderableInfo& a_renderableInfo,
        const GpuData::ObjectTransformGpu& a_transform)
    {
        // 3 つの配列は同じ index で対応するため、追加は全配列で原子的に扱う。
        const size_t oldObjectCount = m_staticMeshObjects.size();
        const size_t oldRenderableInfoCount = m_renderableInfos.size();
        const size_t oldTransformCount = m_transforms.size();

        try
        {
            m_staticMeshObjects.push_back(a_object);
            m_renderableInfos.push_back(a_renderableInfo);
            m_transforms.push_back(a_transform);
        }
        catch (const std::bad_alloc&)
        {
            m_staticMeshObjects.resize(oldObjectCount);
            m_renderableInfos.resize(oldRenderableInfoCount);
            m_transforms.resize(oldTransformCount);
            return Result::fail(Code::OutOfMemory, Severity::Error, "DrawScene out of memory.");
        }

        return Result::ok();
    }

    Result DrawScene::add_camera(const CameraDrawItem& a_camera)
    {
        try
        {
            m_cameras.push_back(a_camera);
        }
        catch (const std::bad_alloc&)
        {
            return Result::fail(Code::OutOfMemory, Severity::Error, "DrawScene out of memory.");
        }

        return Result::ok();
    }

    void DrawScene::clear_cameras() noexcept
    {
        m_cameras.clear();
    }

    const std::vector<StaticMeshDrawObject>& DrawScene::static_mesh_objects() const noexcept
    {
        return m_staticMeshObjects;
    }

    const std::vector<CameraDrawItem>& DrawScene::cameras() const noexcept
    {
        return m_cameras;
    }

    const std::vector<GpuData::RenderableInfo>& DrawScene::renderable_infos() const noexcept
    {
        return m_renderableInfos;
    }

    const std::vector<GpuData::ObjectTransformGpu>& DrawScene::transforms() const noexcept
    {
        return m_transforms;
    }
} // namespace Cue::DrawSystem
