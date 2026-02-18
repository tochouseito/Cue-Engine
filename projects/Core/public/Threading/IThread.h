#pragma once
#include <cstdint>
#include <string>
#include <Result.h>
#include "StopToken.h"

namespace Cue::Core::Threading
{
    struct ThreadDesc final
    {
        // 1) デバッグ用途の名前（UTF-8想定）
        std::string name{};

        // 2) スタックサイズ（0ならOS/CRTデフォルト）
        std::size_t stackSizeBytes = 0;

        // 3) 優先度（実装側で解釈。0はデフォルト）
        int priority = 0;

        // 4) アフィニティ（0なら未指定）
        uint64_t affinityMask = 0;
    };

    using ThreadProc = uint32_t(*)(StopToken token, void* user) noexcept;

    class IThread
    {
    public:
        virtual ~IThread() = default;

        virtual bool joinable() const noexcept = 0;
        virtual Result join() noexcept = 0;

        virtual void request_stop() noexcept = 0;
        virtual StopToken stop_token() const noexcept = 0;

        virtual uint32_t thread_id() const noexcept = 0;
        virtual uint32_t exit_code() const noexcept = 0;
    };
}
