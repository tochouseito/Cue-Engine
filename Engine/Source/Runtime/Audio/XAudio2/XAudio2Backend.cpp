#include "XAudio2Backend.h"

// === Audio includes ===
#include <AudioBackendFactory.h>

// === PAL includes ===
#include <ConvertHresult.h>

// === C++ includes ===
#include <algorithm>
#include <cstring>
#include <limits>

namespace Cue::Audio
{
    namespace
    {
        [[nodiscard]] uint16_t compute_block_align(
            const AudioFormatDesc& a_desc) noexcept
        {
            const uint32_t bytesPerSample =
                static_cast<uint32_t>(a_desc.bitsPerSample) / 8u;
            return static_cast<uint16_t>(
                static_cast<uint32_t>(a_desc.channelCount) * bytesPerSample);
        }

        [[nodiscard]] uint32_t compute_average_bytes_per_second(
            const AudioFormatDesc& a_desc,
            uint16_t a_blockAlign) noexcept
        {
            return a_desc.samplesPerSecond * static_cast<uint32_t>(a_blockAlign);
        }
    }

    std::unique_ptr<IBackend> create_backend()
    {
        return std::make_unique<XAudio2Backend>();
    }

    Result XAudio2Backend::initialize()
    {
        if (m_isInitialized)
        {
            return Result::ok();
        }

        m_isInitialized = true;
        return Result::ok();
    }

    Result XAudio2Backend::shutdown()
    {
        if (!m_isInitialized)
        {
            return Result::ok();
        }

        const std::vector<AudioDeviceHandle> deviceHandles = m_deviceHandles;
        for (const AudioDeviceHandle deviceHandle : deviceHandles)
        {
            const Result result = destroy_device(deviceHandle);
            if (!result)
            {
                return result;
            }
        }

        m_isInitialized = false;
        return Result::ok();
    }

    Result XAudio2Backend::create_device(const AudioDeviceDesc& a_desc,
        AudioDeviceHandle& a_outHandle)
    {
        a_outHandle = {};

        if (!m_isInitialized)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Audio backend is not initialized.");
        }

        if (a_desc.masterVolume < 0.0f)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio device master volume must be non-negative.");
        }

        AudioDeviceRecord record{};
        HRESULT hr = XAudio2Create(
            record.xaudio2.GetAddressOf(), 0u, XAUDIO2_DEFAULT_PROCESSOR);
        Result result =
            convert_xaudio2_result(hr, "XAudio2 device could not be created.");
        if (!result)
        {
            return result;
        }

        hr = record.xaudio2->CreateMasteringVoice(&record.masteringVoice);
        result = convert_xaudio2_result(
            hr, "XAudio2 mastering voice could not be created.");
        if (!result)
        {
            record.xaudio2.Reset();
            return result;
        }

        hr = record.masteringVoice->SetVolume(a_desc.masterVolume);
        result = convert_xaudio2_result(
            hr, "XAudio2 mastering voice volume could not be set.");
        if (!result)
        {
            record.masteringVoice->DestroyVoice();
            record.masteringVoice = nullptr;
            record.xaudio2.Reset();
            return result;
        }

        record.masterVolume = a_desc.masterVolume;
        a_outHandle = m_deviceRegistry.create(record);
        m_deviceHandles.push_back(a_outHandle);
        return Result::ok();
    }

    Result XAudio2Backend::destroy_device(AudioDeviceHandle a_handle)
    {
        AudioDeviceRecord* deviceRecord = m_deviceRegistry.ref_get(a_handle);
        if (deviceRecord == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Audio device was not found.");
        }

        const std::vector<AudioSourceHandle> sourceHandles =
            deviceRecord->sourceHandles;
        for (const AudioSourceHandle sourceHandle : sourceHandles)
        {
            AudioSourceRecord* sourceRecord = m_sourceRegistry.ref_get(sourceHandle);
            if (sourceRecord == nullptr || sourceRecord->deviceHandle != a_handle)
            {
                continue;
            }

            const Result result = destroy_source(sourceHandle);
            if (!result)
            {
                return result;
            }
        }

        deviceRecord = m_deviceRegistry.ref_get(a_handle);
        if (deviceRecord == nullptr)
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Audio device became invalid during destruction.");
        }

        if (deviceRecord->masteringVoice != nullptr)
        {
            deviceRecord->masteringVoice->DestroyVoice();
            deviceRecord->masteringVoice = nullptr;
        }
        deviceRecord->xaudio2.Reset();
        deviceRecord->sourceHandles.clear();

        if (!m_deviceRegistry.destroy(a_handle))
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Audio device registry destroy failed.");
        }

        std::erase(m_deviceHandles, a_handle);
        return Result::ok();
    }

    Result XAudio2Backend::create_source(AudioDeviceHandle a_deviceHandle,
        const AudioSourceDesc& a_desc,
        AudioSourceHandle& a_outHandle)
    {
        a_outHandle = {};

        if (!m_isInitialized)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Audio backend is not initialized.");
        }

        AudioDeviceRecord* deviceRecord = m_deviceRegistry.ref_get(a_deviceHandle);
        if (deviceRecord == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Audio device was not found.");
        }

        if (a_desc.audioData.empty())
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio source data must not be empty.");
        }
        if (a_desc.format.channelCount == 0 ||
            a_desc.format.samplesPerSecond == 0 ||
            a_desc.format.bitsPerSample == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio source format is invalid.");
        }
        if (a_desc.audioData.size() >
            static_cast<size_t>((std::numeric_limits<UINT32>::max)()))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio source data is too large for XAudio2.");
        }

        AudioSourceRecord record{};
        record.deviceHandle = a_deviceHandle;
        record.audioData.assign(a_desc.audioData.begin(), a_desc.audioData.end());
        record.loopBegin = a_desc.loopBegin;
        record.loopLength = a_desc.loopLength;
        record.loopCount = a_desc.loopCount;
        record.isEndOfStream = a_desc.isEndOfStream;

        Result result = build_wave_format_bytes(a_desc.format, record.formatBytes);
        if (!result)
        {
            return result;
        }

        HRESULT hr = deviceRecord->xaudio2->CreateSourceVoice(
            &record.sourceVoice, get_wave_format(record));
        result = convert_xaudio2_result(
            hr, "XAudio2 source voice could not be created.");
        if (!result)
        {
            return result;
        }

        a_outHandle = m_sourceRegistry.create(record);
        deviceRecord->sourceHandles.push_back(a_outHandle);
        return Result::ok();
    }

    Result XAudio2Backend::destroy_source(AudioSourceHandle a_handle)
    {
        AudioSourceRecord* sourceRecord = m_sourceRegistry.ref_get(a_handle);
        if (sourceRecord == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Audio source was not found.");
        }

        if (sourceRecord->sourceVoice != nullptr)
        {
            sourceRecord->sourceVoice->Stop(0u);
            sourceRecord->sourceVoice->FlushSourceBuffers();
            sourceRecord->sourceVoice->DestroyVoice();
            sourceRecord->sourceVoice = nullptr;
        }

        const AudioDeviceHandle deviceHandle = sourceRecord->deviceHandle;
        sourceRecord->audioData.clear();
        sourceRecord->formatBytes.clear();

        if (!m_sourceRegistry.destroy(a_handle))
        {
            return Result::fail(Code::InternalError, Severity::Error,
                "Audio source registry destroy failed.");
        }

        return remove_source_from_device(deviceHandle, a_handle);
    }

    Result XAudio2Backend::play_source(AudioSourceHandle a_handle)
    {
        AudioSourceRecord* sourceRecord = m_sourceRegistry.ref_get(a_handle);
        if (sourceRecord == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Audio source was not found.");
        }
        if (sourceRecord->sourceVoice == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Audio source voice is not valid.");
        }

        HRESULT hr = sourceRecord->sourceVoice->Stop(0u);
        Result result = convert_xaudio2_result(
            hr, "XAudio2 source could not be stopped before playback.");
        if (!result)
        {
            return result;
        }

        hr = sourceRecord->sourceVoice->FlushSourceBuffers();
        result = convert_xaudio2_result(
            hr, "XAudio2 source buffers could not be flushed before playback.");
        if (!result)
        {
            return result;
        }

        XAUDIO2_BUFFER buffer{};
        buffer.AudioBytes = static_cast<UINT32>(sourceRecord->audioData.size());
        buffer.pAudioData =
            reinterpret_cast<const BYTE*>(sourceRecord->audioData.data());
        buffer.LoopBegin = sourceRecord->loopBegin;
        buffer.LoopLength = sourceRecord->loopLength;
        buffer.LoopCount = sourceRecord->loopCount;
        buffer.Flags =
            sourceRecord->isEndOfStream ? XAUDIO2_END_OF_STREAM : 0u;

        hr = sourceRecord->sourceVoice->SubmitSourceBuffer(&buffer);
        result = convert_xaudio2_result(
            hr, "XAudio2 source buffer submission failed.");
        if (!result)
        {
            return result;
        }

        hr = sourceRecord->sourceVoice->Start(0u);
        return convert_xaudio2_result(
            hr, "XAudio2 source playback could not be started.");
    }

    Result XAudio2Backend::stop_source(AudioSourceHandle a_handle)
    {
        AudioSourceRecord* sourceRecord = m_sourceRegistry.ref_get(a_handle);
        if (sourceRecord == nullptr)
        {
            return Result::fail(Code::NotFound, Severity::Error,
                "Audio source was not found.");
        }
        if (sourceRecord->sourceVoice == nullptr)
        {
            return Result::fail(Code::InvalidState, Severity::Error,
                "Audio source voice is not valid.");
        }

        HRESULT hr = sourceRecord->sourceVoice->Stop(0u);
        Result result = convert_xaudio2_result(
            hr, "XAudio2 source could not be stopped.");
        if (!result)
        {
            return result;
        }

        hr = sourceRecord->sourceVoice->FlushSourceBuffers();
        return convert_xaudio2_result(
            hr, "XAudio2 source buffers could not be flushed.");
    }

    Result XAudio2Backend::build_wave_format_bytes(
        const AudioFormatDesc& a_desc,
        std::vector<std::byte>& a_outBytes) noexcept
    {
        a_outBytes.clear();

        if (a_desc.channelCount == 0 ||
            a_desc.samplesPerSecond == 0 ||
            a_desc.bitsPerSample == 0)
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio format contains invalid channel, sample rate, or bit depth.");
        }

        if (a_desc.extraData.size() >
            static_cast<size_t>((std::numeric_limits<uint16_t>::max)()))
        {
            return Result::fail(Code::InvalidArgument, Severity::Error,
                "Audio format extra data is too large.");
        }

        const uint16_t blockAlign = a_desc.blockAlign != 0
            ? a_desc.blockAlign
            : compute_block_align(a_desc);
        const uint32_t averageBytesPerSecond =
            a_desc.averageBytesPerSecond != 0
            ? a_desc.averageBytesPerSecond
            : compute_average_bytes_per_second(a_desc, blockAlign);
        const uint16_t extraDataSize =
            static_cast<uint16_t>(a_desc.extraData.size());

        a_outBytes.resize(sizeof(WAVEFORMATEX) + extraDataSize);
        auto* waveFormat =
            reinterpret_cast<WAVEFORMATEX*>(a_outBytes.data());
        waveFormat->wFormatTag = a_desc.formatTag;
        waveFormat->nChannels = a_desc.channelCount;
        waveFormat->nSamplesPerSec = a_desc.samplesPerSecond;
        waveFormat->nAvgBytesPerSec = averageBytesPerSecond;
        waveFormat->nBlockAlign = blockAlign;
        waveFormat->wBitsPerSample = a_desc.bitsPerSample;
        waveFormat->cbSize = extraDataSize;

        if (extraDataSize != 0)
        {
            std::memcpy(a_outBytes.data() + sizeof(WAVEFORMATEX),
                a_desc.extraData.data(), extraDataSize);
        }

        return Result::ok();
    }

    const WAVEFORMATEX* XAudio2Backend::get_wave_format(
        const AudioSourceRecord& a_record) noexcept
    {
        return reinterpret_cast<const WAVEFORMATEX*>(a_record.formatBytes.data());
    }

    Result XAudio2Backend::convert_xaudio2_result(
        HRESULT a_hresult,
        std::string_view a_message) noexcept
    {
        if (SUCCEEDED(a_hresult))
        {
            return Result::ok();
        }

        return Result::fail(
            PAL::Win::convert_hresult_code(a_hresult),
            Severity::Error,
            a_message);
    }

    Result XAudio2Backend::remove_source_from_device(
        AudioDeviceHandle a_deviceHandle,
        AudioSourceHandle a_sourceHandle) noexcept
    {
        AudioDeviceRecord* deviceRecord = m_deviceRegistry.ref_get(a_deviceHandle);
        if (deviceRecord == nullptr)
        {
            return Result::ok();
        }

        std::erase(deviceRecord->sourceHandles, a_sourceHandle);
        return Result::ok();
    }
}
