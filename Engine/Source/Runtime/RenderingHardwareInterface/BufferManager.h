#pragma once

/// ************************************************************************************
/// バッファマネージャーインタフェース
/// ************************************************************************************

// === RHI Includes ===
#include "RHICommon.h"
#include "SlotUploader.h"

namespace Cue::RHI
{
    struct UploadBufferView
    {
        uint32_t alignment = 0;
        uint32_t stride = 0;
        uint32_t elementCount = 0;
        std::vector<std::byte*> mappedDatas{};
    };

    struct ReadbackBufferView
    {
        uint32_t alignment = 0;
        uint32_t stride = 0;
        uint32_t elementCount = 0;
        std::vector<std::byte*> mappedDatas{};
    };

    struct BufferDesc
    {
        std::string name;
        BufferType type = BufferType::Unknown;
        uint32_t defaultHeapCount = 0; // デフォルトのヒープ数（バッファリングなしの場合は1）
        uint32_t uploadHeapCount = 0; // アップロードヒープの数（アップロードが必要な場合は1以上）
        uint32_t readbackHeapCount = 0; // GPU から CPU へ読み戻すヒープ数
        ResourceState initialState = ResourceState::Common;
        uint32_t stride = 0; // StructuredBufferの要素サイズなど、リソースのインスタンスごとのサイズ
        uint32_t elementCount = 0; // StructuredBufferの要素数など、リソースのインスタンスごとの要素数
        uint32_t size = 0; // バッファ全体のサイズ（stride * インスタンス数など）
        uint32_t alignment = 0; // バッファのアライメント要件
    };

    class IBufferManager
    {
    public:
        IBufferManager() = default;
        // コピー禁止
        IBufferManager(const IBufferManager&) = delete;
        IBufferManager& operator=(const IBufferManager&) = delete;
        // ムーブ禁止
        IBufferManager(IBufferManager&&) = delete;
        IBufferManager& operator=(IBufferManager&&) = delete;
        virtual ~IBufferManager() = default;

        // --- バッファの生成と破棄 ---
        virtual Result create_buffer(const BufferDesc& desc, BufferHandle& out) = 0;
        virtual Result destroy_buffer(BufferHandle handle) = 0;

        // --- 名前からバッファハンドルの取得 ---
        virtual Result get_buffer(std::string_view name, BufferHandle& out) = 0;

        // --- アップローダーの作成 ---
        virtual Result get_upload_buffer_view(BufferHandle handle, UploadBufferView& outView) = 0;
        virtual Result get_readback_buffer_view(BufferHandle handle, ReadbackBufferView& outView) = 0;

        template<typename T>
        Result create_slot_uploaders(
            BufferHandle handle,
            uint32_t bufferCount,
            std::vector<SlotUploader<T>>& outUploaders)
        {
            // - バッファの upload view を解決して、uploader 構築に必要な情報を集める
            UploadBufferView view{};
            Result result = get_upload_buffer_view(handle, view);
            if (!result)
            {
                return result;
            }

            // - 要求数と型契約を検証して、フレームごとの uploader 数を固定する
            if (bufferCount == 0)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Buffer count must be greater than 0.");
            }
            if (view.mappedDatas.size() != bufferCount)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload buffer count does not match the requested buffer count.");
            }
            if (view.stride != sizeof(T))
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "SlotUploader element size does not match the buffer stride.");
            }
            if (view.alignment == 0 || view.elementCount == 0)
            {
                return Result::fail(
                    Code::InvalidArgument,
                    Severity::Error,
                    "Upload buffer view is not initialized.");
            }

            // - 各 upload heap に対応する uploader を値で構築して返す
            outUploaders.clear();
            outUploaders.reserve(bufferCount);
            for (std::byte* mappedData : view.mappedDatas)
            {
                if (mappedData == nullptr)
                {
                    outUploaders.clear();
                    return Result::fail(
                        Code::InternalError,
                        Severity::Error,
                        "Upload buffer mapped pointer is null.");
                }

                SlotUploader<T> uploader{};
                uploader.initialize(view.elementCount, view.alignment, mappedData);
                outUploaders.emplace_back(std::move(uploader));
            }

            return Result::ok();
        }
    };
}
