#include "D3d12ProbeUtilities.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

#include <cstddef>
#include <vector>

namespace cue::d3d12_test_private
{
/// @brief Probe完了時のInfo Queueを走査し、取得失敗も診断異常として数える
std::uint64_t count_info_queue_errors(ID3D12Device *a_device) noexcept
{
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue;

    if (FAILED(a_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        return 0;
    }

    const std::uint64_t messageCount = infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();
    std::uint64_t errorCount = 0;

    for (std::uint64_t messageIndex = 0; messageIndex < messageCount; ++messageIndex)
    {
        SIZE_T messageSize = 0;

        if (FAILED(infoQueue->GetMessage(messageIndex, nullptr, &messageSize)) || messageSize == 0)
        {
            ++errorCount;
            continue;
        }

        try
        {
            std::vector<std::byte> storage(messageSize);
            D3D12_MESSAGE *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());

            if (FAILED(infoQueue->GetMessage(messageIndex, message, &messageSize)) ||
                message->Severity == D3D12_MESSAGE_SEVERITY_ERROR ||
                message->Severity == D3D12_MESSAGE_SEVERITY_CORRUPTION)
            {
                ++errorCount;
            }
        }
        catch (...)
        {
            return errorCount + 1;
        }
    }

    return errorCount;
}
} // namespace cue::d3d12_test_private
