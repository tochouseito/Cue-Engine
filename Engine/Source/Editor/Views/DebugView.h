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
        }
        ~DebugView() = default;
        void update()
        {
            Result viewResult =
                m_backend->get_view_manager()->get_view("FinalColorSRV", m_finalColorSrvHandle);
            if (!viewResult)
            {
                CUE_ASSERTF(false,
                    "Failed to get view: %s (code: %s, severity: %s) at %s:%u in function %s",
                    viewResult.message.data(), Cue::to_string(viewResult.code),
                    Cue::to_string(viewResult.severity), viewResult.file,
                    viewResult.line, viewResult.function);
            }

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

            if (ImGui::Button("Use Camera 0"))
            {
                Result r = editorBridge->submit_command(
                    std::make_unique<Cue::SetMainCameraCommand>(0));
                if (!r)
                {
                    CUE_ASSERTF(false,
                        "Failed to submit set main camera command: %s (code: "
                        "%s, severity: %s) at %s:%u in function %s",
                        r.message.data(), Cue::to_string(r.code),
                        Cue::to_string(r.severity), r.file, r.line,
                        r.function);
                }
            }

            if (ImGui::Button("Use Camera 1"))
            {
                Result r = editorBridge->submit_command(
                    std::make_unique<Cue::SetMainCameraCommand>(1));
                if (!r)
                {
                    CUE_ASSERTF(false,
                        "Failed to submit set main camera command: %s (code: "
                        "%s, severity: %s) at %s:%u in function %s",
                        r.message.data(), Cue::to_string(r.code),
                        Cue::to_string(r.severity), r.file, r.line,
                        r.function);
                }
            }

            D3D12_GPU_DESCRIPTOR_HANDLE finalColorSrvGpuDescHandle =
                m_backend->get_gpu_descriptor_handle(
                    m_finalColorSrvHandle,
                    m_backend->current_back_buffer_index(),
                    m_backend->buffer_count());
            const uint32_t finalColorWidth = m_backend->width();
            const uint32_t finalColorHeight = m_backend->height();
            if (finalColorSrvGpuDescHandle.ptr != 0 &&
                finalColorWidth > 0 && finalColorHeight > 0)
            {
                ImGui::Text("FinalColor");

                const ImVec2 availableRegion = ImGui::GetContentRegionAvail();
                float displayWidth = availableRegion.x;
                float displayHeight =
                    displayWidth * static_cast<float>(finalColorHeight) /
                    static_cast<float>(finalColorWidth);

                if (availableRegion.y > 0.0f && displayHeight > availableRegion.y)
                {
                    const float scale = availableRegion.y / displayHeight;
                    displayWidth *= scale;
                    displayHeight *= scale;
                }

                if (displayWidth <= 0.0f || displayHeight <= 0.0f)
                {
                    ImGui::End();
                    return;
                }

                ImGui::Image(
                    static_cast<ImTextureID>(finalColorSrvGpuDescHandle.ptr),
                    ImVec2(displayWidth, displayHeight));
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
