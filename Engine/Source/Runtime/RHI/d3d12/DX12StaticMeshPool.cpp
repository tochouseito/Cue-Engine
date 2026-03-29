#include "DX12StaticMeshPool.h"

namespace Cue::RHI::DX12
{
    DX12StaticMeshPool::DX12StaticMeshPool(const StaticMeshPoolDesc& desc, DX12BufferManager& bufferManager)
        : m_bufferManager(bufferManager)
    {
        (void)desc;
    }
    Result DX12StaticMeshPool::allocate_mesh(const Core::Native::MeshData& meshData, StaticMeshHandle& outHandle)
    {
        (void)meshData;
        (void)outHandle;
        return Result();
    }
    Result DX12StaticMeshPool::free_mesh(StaticMeshHandle handle)
    {
        (void)handle;
        return Result();
    }
}
