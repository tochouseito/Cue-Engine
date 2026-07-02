// Amplification shader dispatch stage for mesh shader visibility.

struct VisibleMeshlet
{
    uint objectIndex;
    uint meshId;
    uint meshletIndex;
    uint segmentStartIndex;
};

struct MeshShaderVisibilityPayload
{
    uint visibleMeshletBaseIndex;
};

ByteAddressBuffer g_meshShaderVisibilityCounters : register(t9);

static const uint kVisibleMeshletCounterOffset = 0u;
static const uint kMeshletsPerAmplificationGroup = 64u;

[numthreads(1, 1, 1)]
void as_main(uint3 groupId : SV_GroupID)
{
    MeshShaderVisibilityPayload payload;
    payload.visibleMeshletBaseIndex =
        groupId.x * kMeshletsPerAmplificationGroup;

    const uint visibleMeshletCount =
        g_meshShaderVisibilityCounters.Load(kVisibleMeshletCounterOffset);
    const uint remainingMeshletCount =
        visibleMeshletCount > payload.visibleMeshletBaseIndex
            ? visibleMeshletCount - payload.visibleMeshletBaseIndex
            : 0u;
    const uint dispatchMeshletCount =
        min(remainingMeshletCount, kMeshletsPerAmplificationGroup);
    DispatchMesh(dispatchMeshletCount, 1, 1, payload);
}
