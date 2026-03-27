#include "Engine.h"

namespace Cue
{
    Result Engine::initialize(const EngineSetupInfo& a_info)
    {
        // 引数の検査
        if (!a_info.platform || !a_info.backend)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error, "Invalid argument: platform and backend must not be null");
        }

        // 依存オブジェクトの保存
        m_platform = a_info.platform;
        m_backend = a_info.backend;

        // フレームコントローラーの生成
        FrameControllerDesc desc{};
        desc.m_bufferCount = a_info.bufferCount;
        desc.m_mode = ControllerMode::Fixed;
        desc.m_maxFps = a_info.maxFps;
        m_frameController = std::make_unique<FrameController>(
            desc,
            m_platform->thread_factory(),
            m_platform->clock(),
            m_platform->waiter(),
            update(), render(), present());

        // テスト
        auto bufferManager = m_backend->get_buffer_manager();
        RHI::BufferDesc bufferDesc{};
        bufferDesc.name = "TestBuffer";
        bufferDesc.type = RHI::BufferType::Constant;
        bufferDesc.defaultHeapCount = 3;
        bufferDesc.uploadHeapCount = 3;
        bufferDesc.initialState = RHI::ResourceState::Common;
        bufferDesc.stride = sizeof(Core::Native::ObjectTransformGpu);
        bufferDesc.elementCount = 1;
        bufferDesc.size = bufferDesc.stride * bufferDesc.elementCount;
        bufferDesc.alignment = 256;
        RHI::BufferHandle bufferHandle{};
        Result result = bufferManager->create_buffer(bufferDesc, bufferHandle);
        if (!result)
        {
            CUE_ASSERT(false);
        }
        auto viewManager = m_backend->get_view_manager();
        RHI::ViewDesc viewDesc{};
        viewDesc.name = "TestView";
        viewDesc.type = RHI::ViewType::ConstantBuffer;
        viewDesc.bufferKind = RHI::BufferKind::Buffer;
        viewDesc.bufferHandle = bufferHandle;
        viewDesc.byteOffset = 0;
        viewDesc.byteSize = bufferDesc.size;
        viewDesc.structureByteStride = bufferDesc.stride;
        viewDesc.numElements = bufferDesc.elementCount;
        RHI::ViewHandle viewHandle{};
        result = viewManager->create_view(viewDesc, viewHandle);
        if (!result)
        {
            CUE_ASSERT(false);
        }
        viewManager->destroy_view(viewHandle);
        bufferManager->destroy_buffer(bufferHandle);

        return Result::ok();
    }

    void Engine::shutdown()
    {
        
    }

    Result Engine::begin_frame()
    {
        return Result::ok();
    }

    Result Engine::end_frame()
    {
        return Result::ok();
    }

    Result Engine::tick()
    {
        m_frameController->step();

        return Result::ok();
    }

    std::function<void(uint64_t, uint32_t)> Engine::update()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                a_frameNo; a_index;
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::render()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                a_frameNo; a_index;
            };
    }

    std::function<void(uint64_t, uint32_t)> Engine::present()
    {
        return [this](uint64_t a_frameNo, uint32_t a_index)
            {
                a_frameNo; a_index;
            };
    }
}
