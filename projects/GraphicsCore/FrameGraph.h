#pragma once

// === C++ standard library includes ===
#include <cstdint>
#include <vector>

// === Base includes ===
#include <Result.h>

#include "ResourceHandle.h"

namespace Cue::GraphicsCore
{
    enum class PassType : uint8_t
    {
        Render,
        Compute,
        Copy,
    };

    enum class ResourceKind : uint8_t
    {
        Texture,
        Buffer
    };

    enum class ResourceAccessType : uint8_t
    {
        Read,
        Write
    };

    enum class ResourceStates : uint8_t
    {
        Common = 0,
    };

    struct ResourceAccess final
    {
        ResourceHandle m_handle{};
        ResourceStates m_state = ResourceStates::Common;
        ResourceAccessType m_access = ResourceAccessType::Read;
        bool m_hasFinalState = false;
        ResourceStates m_finalState = ResourceStates::Common;
    };

    struct PassNode final
    {
        FrameGraphPass* m_pass = nullptr;
        PassType m_type = PassType::Render;
        bool m_allowAsyncCompute = false;
        bool m_allowCopyQueue = false;
        std::vector<ResourceAccess> m_accesses;
        std::vector<uint32_t> m_dependencies;
    };

    class FrameGraphBuilder
    {
    public:
        FrameGraphBuilder(uint32_t passIndex)
            : m_passIndex(passIndex)
        {}

    private:
        uint32_t m_passIndex = 0;
    };

    class FrameGraphContext
    {

    };

    class FrameGraphPass
    {
    public:
        virtual ~FrameGraphPass() = default;
        virtual const char* name() const = 0;
        virtual PassType type() const = 0;
        virtual void setup(FrameGraphBuilder& builder) = 0;
        virtual void execute(FrameGraphContext& context) = 0;
    };

    class PassCompiler
    {};

    class PassExecutor
    {};

    class FrameGraph final
    {
    public:
        FrameGraph();
        ~FrameGraph();

        void add_pass(FrameGraphPass& pass);
        Result build();
        Result execute();
    private:
        Result build_dependencies();
        Result build_execution_order();
    private:
        bool m_forceRebuild = false;
        bool m_buildCacheValid = false;

        std::vector<PassNode> m_passes;
    };
}
