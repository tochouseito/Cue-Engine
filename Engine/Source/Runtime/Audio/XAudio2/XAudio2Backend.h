#pragma once

// === Audio includes ===
#include <Audio.h>

// === Core includes ===
#include <Container/Registry.h>

// === C++ includes ===
#include <cstddef>
#include <cstdint>
#include <vector>

// === Windows includes ===
#include <wrl.h>
#include <xaudio2.h>

namespace Cue::Audio
{
    class XAudio2Backend final : public IBackend
    {
    public:
        XAudio2Backend() = default;
        XAudio2Backend(const XAudio2Backend&) = delete;
        XAudio2Backend& operator=(const XAudio2Backend&) = delete;
        XAudio2Backend(XAudio2Backend&&) = delete;
        XAudio2Backend& operator=(XAudio2Backend&&) = delete;
        ~XAudio2Backend() override = default;

        Result initialize() override;
        Result shutdown() override;

        Result create_device(const AudioDeviceDesc& a_desc,
            AudioDeviceHandle& a_outHandle) override;
        Result destroy_device(AudioDeviceHandle a_handle) override;

        Result create_source(AudioDeviceHandle a_deviceHandle,
            const AudioSourceDesc& a_desc,
            AudioSourceHandle& a_outHandle) override;
        Result destroy_source(AudioSourceHandle a_handle) override;

        Result play_source(AudioSourceHandle a_handle) override;
        Result stop_source(AudioSourceHandle a_handle) override;
        Result set_source_volume(
            AudioSourceHandle a_handle, float a_volume) override;

    private:
        struct AudioDeviceRecord final
        {
            Microsoft::WRL::ComPtr<IXAudio2> xaudio2{};
            IXAudio2MasteringVoice* masteringVoice = nullptr;
            float masterVolume = 1.0f;
            std::vector<AudioSourceHandle> sourceHandles{};
        };

        struct AudioSourceRecord final
        {
            AudioDeviceHandle deviceHandle{};
            IXAudio2SourceVoice* sourceVoice = nullptr;
            std::vector<std::byte> formatBytes{};
            std::vector<std::byte> audioData{};
            uint32_t loopBegin = 0;
            uint32_t loopLength = 0;
            uint32_t loopCount = 0;
            float volume = 1.0f;
            bool isEndOfStream = true;
        };

        [[nodiscard]] static Result build_wave_format_bytes(
            const AudioFormatDesc& a_desc,
            std::vector<std::byte>& a_outBytes) noexcept;
        [[nodiscard]] static const WAVEFORMATEX* get_wave_format(
            const AudioSourceRecord& a_record) noexcept;
        [[nodiscard]] static Result convert_xaudio2_result(HRESULT a_hresult,
            std::string_view a_message) noexcept;
        [[nodiscard]] Result remove_source_from_device(
            AudioDeviceHandle a_deviceHandle,
            AudioSourceHandle a_sourceHandle) noexcept;

    private:
        bool m_isInitialized = false;
        Core::Registry<AudioDeviceTag, AudioDeviceRecord> m_deviceRegistry{};
        std::vector<AudioDeviceHandle> m_deviceHandles{};
        Core::Registry<AudioSourceTag, AudioSourceRecord> m_sourceRegistry{};
    };
}
