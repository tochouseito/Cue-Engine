#include "StaticMeshBatcher.h"

// === Engine includes ===
#include "DrawSystem/DrawScene.h"
#include "DrawSystem/MeshPool.h"

// === C++ includes ===
#include <algorithm>
#include <limits>
#include <new>

namespace Cue::DrawSystem
{
    namespace
    {
        /// @brief ソート後に同じキーの object を連続配置するための中間要素
        struct StaticMeshSortEntry final
        {
            StaticMeshBatchKey key{}; // mesh/material の分類キー
            uint32_t objectIndex = 0; // DrawScene::static_mesh_objects 上の index
        };

        [[nodiscard]] bool less_batch_key(
            const StaticMeshBatchKey& a_left,
            const StaticMeshBatchKey& a_right) noexcept
        {
            if (a_left.meshId != a_right.meshId)
            {
                return a_left.meshId < a_right.meshId;
            }

            return a_left.materialId < a_right.materialId;
        }

        [[nodiscard]] bool equals_batch_key(
            const StaticMeshBatchKey& a_left,
            const StaticMeshBatchKey& a_right) noexcept
        {
            return a_left.meshId == a_right.meshId && a_left.materialId == a_right.materialId;
        }

        [[nodiscard]] bool less_sort_entry(
            const StaticMeshSortEntry& a_left,
            const StaticMeshSortEntry& a_right) noexcept
        {
            if (less_batch_key(a_left.key, a_right.key))
            {
                return true;
            }
            if (less_batch_key(a_right.key, a_left.key))
            {
                return false;
            }

            return a_left.objectIndex < a_right.objectIndex;
        }

        [[nodiscard]] GpuData::IndirectCommand create_indirect_command(
            const StaticMeshBatch& a_batch) noexcept
        {
            GpuData::IndirectCommand command{};
            command.drawObjectStartIndex = a_batch.firstObjectIndex;
            command.indexCountPerInstance = a_batch.meshRange.indexCount;
            command.instanceCount = a_batch.objectCount;
            command.startIndexLocation = a_batch.meshRange.startIndex;
            command.baseVertexLocation = a_batch.meshRange.baseVertex;
            command.startInstanceLocation = 0;
            return command;
        }
    } // namespace

    Result StaticMeshBatcher::build_indirect_commands(
        const DrawScene& a_scene,
        const MeshPool& a_meshPool,
        StaticMeshBatchBuildResult& a_outResult)
    {
        a_outResult.batches.clear();
        a_outResult.commands.clear();
        a_outResult.groupedObjectIndices.clear();

        const std::vector<StaticMeshDrawObject>& objects = a_scene.static_mesh_objects();
        if (objects.size() > (std::numeric_limits<uint32_t>::max)())
        {
            return Result::fail(Code::InvalidState, Severity::Error, "StaticMesh object count exceeds uint32_t range.");
        }

        try
        {
            std::vector<StaticMeshSortEntry> sortEntries{};
            sortEntries.reserve(objects.size());

            for (size_t objectIndex = 0; objectIndex < objects.size(); ++objectIndex)
            {
                const StaticMeshDrawObject& object = objects[objectIndex];

                // IndirectCommand batching は不透明 StaticMesh を描画単位としてまとめる。
                if (!object.visible || object.renderQueue != DrawRenderQueue::Opaque)
                {
                    continue;
                }

                StaticMeshSortEntry entry{};
                entry.key.meshId = object.meshId;
                entry.key.materialId = object.materialId;
                entry.objectIndex = static_cast<uint32_t>(objectIndex);
                sortEntries.push_back(entry);
            }

            std::sort(sortEntries.begin(), sortEntries.end(), less_sort_entry);

            a_outResult.batches.reserve(sortEntries.size());
            a_outResult.commands.reserve(sortEntries.size());
            a_outResult.groupedObjectIndices.reserve(sortEntries.size());

            size_t rangeStart = 0;
            while (rangeStart < sortEntries.size())
            {
                const StaticMeshBatchKey batchKey = sortEntries[rangeStart].key;
                size_t rangeEnd = rangeStart + 1;
                while (rangeEnd < sortEntries.size() && equals_batch_key(batchKey, sortEntries[rangeEnd].key))
                {
                    ++rangeEnd;
                }

                MeshRange meshRange{};
                Result result = a_meshPool.get_mesh_range(batchKey.meshId, meshRange);
                if (!result)
                {
                    return result;
                }

                const size_t objectCount = rangeEnd - rangeStart;
                if (objectCount > (std::numeric_limits<uint32_t>::max)())
                {
                    return Result::fail(
                        Code::InvalidState,
                        Severity::Error,
                        "StaticMesh batch size exceeds uint32_t range.");
                }

                StaticMeshBatch batch{};
                batch.key = batchKey;
                batch.meshRange = meshRange;
                batch.firstObjectIndex = static_cast<uint32_t>(a_outResult.groupedObjectIndices.size());
                batch.objectCount = static_cast<uint32_t>(objectCount);

                for (size_t entryIndex = rangeStart; entryIndex < rangeEnd; ++entryIndex)
                {
                    a_outResult.groupedObjectIndices.push_back(sortEntries[entryIndex].objectIndex);
                }

                a_outResult.commands.push_back(create_indirect_command(batch));
                a_outResult.batches.push_back(batch);

                rangeStart = rangeEnd;
            }
        }
        catch (const std::bad_alloc&)
        {
            a_outResult.batches.clear();
            a_outResult.commands.clear();
            a_outResult.groupedObjectIndices.clear();
            return Result::fail(Code::OutOfMemory, Severity::Error, "StaticMeshBatcher out of memory.");
        }

        return Result::ok();
    }
} // namespace Cue::DrawSystem
