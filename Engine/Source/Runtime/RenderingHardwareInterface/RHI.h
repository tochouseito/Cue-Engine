#pragma once

/// ************************************************************************************
/// グラフィックスAPIの抽象化レイヤー
/// ************************************************************************************

// === Base includes ===
#include <CueResult.h>

// === C++ includes ===
#include <cstdint>

namespace Cue::RHI
{
    /// @brief レンダーバックエンドの初期化に必要な情報
    struct RenderBackendSetupInfo final
    {
        bool enableDebugLayer = false;
        uint32_t width{};
        uint32_t height{};
        uint32_t bufferCount = 3;
        uint32_t textureCapacity = 256;
        uint32_t bufferCapacity = 256;
        uint32_t renderTargetCapacity = 16;
        uint32_t depthStencilCapacity = 16;
    };

    /// @brief レンダーバックエンドのインターフェース
    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        /// @brief バックエンドを初期化する
        virtual Result initialize(const RenderBackendSetupInfo& a_info) = 0;

        /// @brief バックエンドを終了し
        virtual Result shutdown() = 0;

        // --- バックエンドのシステムへのアクセス ---
        
        // --- パラメーターの取得 ---
        virtual uint32_t width() const noexcept = 0;
        virtual uint32_t height() const noexcept = 0;
        virtual const uint32_t& buffer_count() const noexcept = 0;
    };
}
