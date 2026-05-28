// Audio の役割と公開要素を定義する

#pragma once

// === Base includes ===
#include <Result.h>

// === Core includes ===
#include <Native/Handle.h>

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <span>

namespace Cue::Audio
{
    struct AudioDeviceTag {};
    struct AudioSourceTag {};

    using AudioDeviceHandle = Core::Handle<AudioDeviceTag>;
    using AudioSourceHandle = Core::Handle<AudioSourceTag>;

    struct AudioDeviceDesc final
    {
        float masterVolume = 1.0f;
    };

    struct AudioFormatDesc final
    {
        uint16_t formatTag = 1;
        uint16_t channelCount = 2;
        uint32_t samplesPerSecond = 44100;
        uint32_t averageBytesPerSecond = 0;
        uint16_t blockAlign = 0;
        uint16_t bitsPerSample = 16;
        std::span<const std::byte> extraData{};
    };

    struct AudioSourceDesc final
    {
        AudioFormatDesc format{};
        std::span<const std::byte> audioData{};
        uint32_t loopBegin = 0;
        uint32_t loopLength = 0;
        uint32_t loopCount = 0;
        float volume = 1.0f;
        bool isEndOfStream = true;
    };

    class IBackend
    {
    public:
        virtual ~IBackend() = default;

        virtual Result initialize() = 0;
        virtual Result shutdown() = 0;

        virtual Result create_device(const AudioDeviceDesc& a_desc,
            AudioDeviceHandle& a_outHandle) = 0;
        virtual Result destroy_device(AudioDeviceHandle a_handle) = 0;

        virtual Result create_source(AudioDeviceHandle a_deviceHandle,
            const AudioSourceDesc& a_desc,
            AudioSourceHandle& a_outHandle) = 0;
        virtual Result destroy_source(AudioSourceHandle a_handle) = 0;

        virtual Result play_source(AudioSourceHandle a_handle) = 0;
        virtual Result stop_source(AudioSourceHandle a_handle) = 0;
        virtual Result set_source_volume(
            AudioSourceHandle a_handle, float a_volume) = 0;
    };
}
