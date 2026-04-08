#pragma once

// === D3D12 includes ===
#include <D3D12Backend.h>

// === Engine includes ===
#include <Commands.h>

// === C++ includes ===
#include <cstdint>
#include <memory>
#include <deque>

// === ImGui includes ===
#include <imgui.h>

namespace Cue::Editor
{
    class DebugView final
    {
    public:
        DebugView(RHI::DX12::D3D12Backend* backend, Core::CQRS::Bridge* bridge)
            : m_backend(backend), editorBridge(bridge)
        {
            Result r = backend->get_view_manager()->get_view("FinalColorSRV", m_finalColorSrvHandle);
            if (!r)
            {
                CUE_ASSERTF(false,
                    "Failed to get view: %s (code: %s, severity: %s) at %s:%u in function %s",
                    r.message.data(), Cue::to_string(r.code),
                    Cue::to_string(r.severity), r.file, r.line, r.function);
            }
        }
        ~DebugView() = default;
        void update()
        {
            ImGui::Begin("Debug View");

            if (ImGui::Button("Add Object"))
            {
                Result r = editorBridge->submit_command(std::make_unique<Cue::AddObjectCommand>());
                if (!r)
                {
                    CUE_ASSERTF(false,
                        "Failed to submit add object command: %s (code: %s, "
                        "severity: %s) at %s:%u in function %s",
                        r.message.data(), Cue::to_string(r.code),
                        Cue::to_string(r.severity), r.file, r.line, r.function);
                }
            }

            ImGui::InputInt("Object Id", &removeObjectId);

            if (ImGui::Button("Remove Object"))
            {
                if (removeObjectId < 0)
                {
                    CUE_ASSERTF(false,
                        "Remove object id must be greater than or equal to 0.");
                }
                else
                {
                    Result r = editorBridge->submit_command(
                        std::make_unique<Cue::RemoveObjectCommand>(
                            static_cast<uint32_t>(removeObjectId)));
                    if (!r)
                    {
                        CUE_ASSERTF(false,
                            "Failed to submit remove object command: %s (code: "
                            "%s, severity: %s) at %s:%u in function %s",
                            r.message.data(), Cue::to_string(r.code),
                            Cue::to_string(r.severity), r.file, r.line,
                            r.function);
                    }
                }
            }

            D3D12_GPU_DESCRIPTOR_HANDLE finalColorSrvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    m_finalColorSrvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            if (finalColorSrvGpuDescHandle.ptr != 0)
            {
                const float finalColorWidth = 640.0f;
                const float aspectRatio =
                    static_cast<float>(m_backend->height()) / static_cast<float>(m_backend->width());
                ImGui::Text("FinalColor");
                ImGui::Image(
                    static_cast<ImTextureID>(finalColorSrvGpuDescHandle.ptr),
                    ImVec2(finalColorWidth, finalColorWidth * aspectRatio));
            }

            ImGui::End();
        }
    private:
        Core::CQRS::Bridge* editorBridge = nullptr;
        RHI::DX12::D3D12Backend* m_backend = nullptr;
        RHI::ViewHandle m_finalColorSrvHandle{};
        int removeObjectId = 0;
    };
}
