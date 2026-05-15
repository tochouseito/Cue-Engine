#include "SoundCooker.h"

// === Audio Includes ===
#include <Audio.h>

// === Engine Includes ===
#include <asset/SoundAssetFormat.h>

// === C++ Includes ===
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <type_traits>
#include <vector>

// === Win Includes ===
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

namespace Cue::Editor
{
    namespace
    {
        static_assert(std::is_trivially_copyable_v<CueSoundHeader>);

        struct WavData final
        {
            Audio::AudioFormatDesc format{};
            std::vector<std::byte> extraData{};
            std::vector<std::byte> audioData{};
        };

        [[nodiscard]] uint16_t read_u16(const std::byte* a_data) noexcept
        {
            return static_cast<uint16_t>(
                static_cast<uint16_t>(std::to_integer<uint8_t>(a_data[0])) |
                (static_cast<uint16_t>(std::to_integer<uint8_t>(a_data[1])) << 8));
        }

        [[nodiscard]] uint32_t read_u32(const std::byte* a_data) noexcept
        {
            return static_cast<uint32_t>(
                static_cast<uint32_t>(std::to_integer<uint8_t>(a_data[0])) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(a_data[1])) << 8) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(a_data[2])) << 16) |
                (static_cast<uint32_t>(std::to_integer<uint8_t>(a_data[3])) << 24));
        }

        void write_i16(
            std::vector<std::byte>& a_buffer,
            size_t a_offset,
            int16_t a_value) noexcept
        {
            const uint16_t value = static_cast<uint16_t>(a_value);
            a_buffer[a_offset + 0] =
                static_cast<std::byte>(value & 0xffu);
            a_buffer[a_offset + 1] =
                static_cast<std::byte>((value >> 8) & 0xffu);
        }

        [[nodiscard]] int16_t clamp_to_i16(int32_t a_value) noexcept
        {
            const int32_t clamped =
                std::clamp(a_value, -32768, 32767);
            return static_cast<int16_t>(clamped);
        }

        [[nodiscard]] int32_t read_pcm_sample(
            const std::byte* a_sample,
            uint16_t a_bitsPerSample,
            uint16_t a_formatTag) noexcept
        {
            if (a_formatTag == 3 && a_bitsPerSample == 32)
            {
                float value = 0.0f;
                std::memcpy(&value, a_sample, sizeof(float));
                const float clamped = std::clamp(value, -1.0f, 1.0f);
                return static_cast<int32_t>(clamped * 32767.0f);
            }

            switch (a_bitsPerSample)
            {
            case 8:
                return (static_cast<int32_t>(
                    std::to_integer<uint8_t>(a_sample[0])) - 128) << 8;
            case 16:
                return static_cast<int16_t>(read_u16(a_sample));
            case 24:
            {
                int32_t value =
                    static_cast<int32_t>(
                        std::to_integer<uint8_t>(a_sample[0])) |
                    (static_cast<int32_t>(
                        std::to_integer<uint8_t>(a_sample[1])) << 8) |
                    (static_cast<int32_t>(
                        std::to_integer<uint8_t>(a_sample[2])) << 16);
                if ((value & 0x800000) != 0)
                {
                    value |= ~0xffffff;
                }
                return value >> 8;
            }
            case 32:
                return static_cast<int32_t>(read_u32(a_sample)) >> 16;
            default:
                return 0;
            }
        }

        [[nodiscard]] Result convert_to_pcm16(
            const WavData& a_source,
            WavData& a_outPcm)
        {
            a_outPcm = {};
            if (a_source.format.channelCount == 0 ||
                a_source.format.samplesPerSecond == 0 ||
                a_source.format.bitsPerSample == 0 ||
                a_source.format.blockAlign == 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound source format is invalid.");
            }

            const uint16_t bytesPerSample =
                static_cast<uint16_t>(a_source.format.bitsPerSample / 8u);
            if (bytesPerSample == 0 ||
                a_source.format.blockAlign <
                    bytesPerSample * a_source.format.channelCount)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Sound source bit depth is not supported.");
            }

            const size_t frameCount =
                a_source.audioData.size() / a_source.format.blockAlign;
            a_outPcm.audioData.resize(
                frameCount * a_source.format.channelCount * sizeof(int16_t));

            size_t writeOffset = 0;
            for (size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
            {
                const std::byte* frame =
                    a_source.audioData.data() +
                    frameIndex * a_source.format.blockAlign;
                for (uint16_t channel = 0;
                     channel < a_source.format.channelCount;
                     ++channel)
                {
                    const std::byte* sample =
                        frame + static_cast<size_t>(channel) * bytesPerSample;
                    write_i16(
                        a_outPcm.audioData,
                        writeOffset,
                        clamp_to_i16(read_pcm_sample(
                            sample,
                            a_source.format.bitsPerSample,
                            a_source.format.formatTag)));
                    writeOffset += sizeof(int16_t);
                }
            }

            a_outPcm.format.formatTag = 1;
            a_outPcm.format.channelCount = a_source.format.channelCount;
            a_outPcm.format.samplesPerSecond =
                a_source.format.samplesPerSecond;
            a_outPcm.format.bitsPerSample = 16;
            a_outPcm.format.blockAlign =
                static_cast<uint16_t>(a_outPcm.format.channelCount * 2u);
            a_outPcm.format.averageBytesPerSecond =
                a_outPcm.format.samplesPerSecond *
                a_outPcm.format.blockAlign;
            return Result::ok();
        }

        [[nodiscard]] Result make_wave_format_bytes(
            const WavData& a_sound,
            std::vector<std::byte>& a_outBytes)
        {
            if (a_sound.extraData.size() >
                static_cast<size_t>((std::numeric_limits<uint16_t>::max)()))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound format extra data is too large.");
            }

            a_outBytes.resize(sizeof(WAVEFORMATEX) + a_sound.extraData.size());
            auto* format =
                reinterpret_cast<WAVEFORMATEX*>(a_outBytes.data());
            format->wFormatTag = a_sound.format.formatTag;
            format->nChannels = a_sound.format.channelCount;
            format->nSamplesPerSec = a_sound.format.samplesPerSecond;
            format->nAvgBytesPerSec = a_sound.format.averageBytesPerSecond;
            format->nBlockAlign = a_sound.format.blockAlign;
            format->wBitsPerSample = a_sound.format.bitsPerSample;
            format->cbSize = static_cast<WORD>(a_sound.extraData.size());
            if (!a_sound.extraData.empty())
            {
                std::memcpy(
                    a_outBytes.data() + sizeof(WAVEFORMATEX),
                    a_sound.extraData.data(),
                    a_sound.extraData.size());
            }
            return Result::ok();
        }

        [[nodiscard]] Result convert_pcm16_to_adpcm(
            const WavData& a_pcm,
            WavData& a_outAdpcm)
        {
            a_outAdpcm = {};

            std::vector<std::byte> sourceFormatBytes{};
            Result result = make_wave_format_bytes(a_pcm, sourceFormatBytes);
            if (!result)
            {
                return result;
            }

            std::vector<std::byte> destinationFormatBytes(
                sizeof(ADPCMWAVEFORMAT));
            auto* sourceFormat =
                reinterpret_cast<WAVEFORMATEX*>(sourceFormatBytes.data());
            auto* destinationFormat =
                reinterpret_cast<WAVEFORMATEX*>(destinationFormatBytes.data());
            destinationFormat->wFormatTag = WAVE_FORMAT_ADPCM;
            destinationFormat->nChannels = sourceFormat->nChannels;
            destinationFormat->nSamplesPerSec = sourceFormat->nSamplesPerSec;

            MMRESULT mmResult = acmFormatSuggest(
                nullptr,
                sourceFormat,
                destinationFormat,
                static_cast<DWORD>(destinationFormatBytes.size()),
                ACM_FORMATSUGGESTF_WFORMATTAG |
                    ACM_FORMATSUGGESTF_NCHANNELS |
                    ACM_FORMATSUGGESTF_NSAMPLESPERSEC);
            if (mmResult != MMSYSERR_NOERROR)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Sound ADPCM format is not supported by ACM.");
            }

            HACMSTREAM stream = nullptr;
            mmResult = acmStreamOpen(
                &stream,
                nullptr,
                sourceFormat,
                destinationFormat,
                nullptr,
                0,
                0,
                ACM_STREAMOPENF_NONREALTIME);
            if (mmResult != MMSYSERR_NOERROR || stream == nullptr)
            {
                return Result::fail(Code::CreateFailed, Severity::Error,
                    "Sound ADPCM conversion stream could not be created.");
            }

            DWORD destinationSize = 0;
            mmResult = acmStreamSize(
                stream,
                static_cast<DWORD>(a_pcm.audioData.size()),
                &destinationSize,
                ACM_STREAMSIZEF_SOURCE);
            if (mmResult != MMSYSERR_NOERROR || destinationSize == 0)
            {
                acmStreamClose(stream, 0);
                return Result::fail(Code::InternalError, Severity::Error,
                    "Sound ADPCM destination size could not be calculated.");
            }

            std::vector<std::byte> destinationData(destinationSize);
            ACMSTREAMHEADER header{};
            header.cbStruct = sizeof(header);
            header.pbSrc =
                reinterpret_cast<LPBYTE>(
                    const_cast<std::byte*>(a_pcm.audioData.data()));
            header.cbSrcLength = static_cast<DWORD>(a_pcm.audioData.size());
            header.pbDst = reinterpret_cast<LPBYTE>(destinationData.data());
            header.cbDstLength = destinationSize;

            mmResult = acmStreamPrepareHeader(stream, &header, 0);
            if (mmResult != MMSYSERR_NOERROR)
            {
                acmStreamClose(stream, 0);
                return Result::fail(Code::InternalError, Severity::Error,
                    "Sound ADPCM conversion header could not be prepared.");
            }

            mmResult = acmStreamConvert(
                stream,
                &header,
                ACM_STREAMCONVERTF_BLOCKALIGN);
            const MMRESULT unprepareResult =
                acmStreamUnprepareHeader(stream, &header, 0);
            acmStreamClose(stream, 0);
            if (mmResult != MMSYSERR_NOERROR ||
                unprepareResult != MMSYSERR_NOERROR)
            {
                return Result::fail(Code::InternalError, Severity::Error,
                    "Sound ADPCM conversion failed.");
            }

            destinationData.resize(header.cbDstLengthUsed);
            const size_t extraDataSize = destinationFormat->cbSize;
            a_outAdpcm.extraData.resize(extraDataSize);
            if (extraDataSize != 0)
            {
                std::memcpy(
                    a_outAdpcm.extraData.data(),
                    destinationFormatBytes.data() + sizeof(WAVEFORMATEX),
                    extraDataSize);
            }
            a_outAdpcm.audioData = std::move(destinationData);
            a_outAdpcm.format.formatTag = destinationFormat->wFormatTag;
            a_outAdpcm.format.channelCount = destinationFormat->nChannels;
            a_outAdpcm.format.samplesPerSecond =
                destinationFormat->nSamplesPerSec;
            a_outAdpcm.format.averageBytesPerSecond =
                destinationFormat->nAvgBytesPerSec;
            a_outAdpcm.format.blockAlign = destinationFormat->nBlockAlign;
            a_outAdpcm.format.bitsPerSample = destinationFormat->wBitsPerSample;
            a_outAdpcm.format.extraData =
                std::span<const std::byte>(
                    a_outAdpcm.extraData.data(),
                    a_outAdpcm.extraData.size());
            return Result::ok();
        }

        [[nodiscard]] bool has_fourcc(
            const std::byte* a_data,
            const char (&a_fourCc)[5]) noexcept
        {
            return std::memcmp(a_data, a_fourCc, 4) == 0;
        }

        [[nodiscard]] Result load_wav(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            WavData& a_outWavData)
        {
            a_outWavData = {};
            if (a_filePath.extension() != ".wav")
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Sound cooker supports wav files only.");
            }

            std::vector<std::byte> fileData{};
            Result result = a_fileSystem.read_all(a_filePath, &fileData);
            if (!result)
            {
                return result;
            }

            if (fileData.size() < 12 ||
                !has_fourcc(fileData.data(), "RIFF") ||
                !has_fourcc(fileData.data() + 8, "WAVE"))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound source is not a RIFF WAVE file.");
            }

            bool hasFormat = false;
            bool hasData = false;
            size_t offset = 12;
            while (offset + 8 <= fileData.size())
            {
                const std::byte* chunkHeader = fileData.data() + offset;
                const uint32_t chunkSize = read_u32(chunkHeader + 4);
                const size_t chunkDataOffset = offset + 8;
                const size_t chunkDataEnd =
                    chunkDataOffset + static_cast<size_t>(chunkSize);
                if (chunkDataEnd > fileData.size())
                {
                    return Result::fail(Code::InvalidArgument, Severity::Error,
                        "Sound wav chunk is invalid.");
                }

                if (has_fourcc(chunkHeader, "fmt "))
                {
                    if (chunkSize < 16)
                    {
                        return Result::fail(Code::InvalidArgument, Severity::Error,
                            "Sound wav fmt chunk is invalid.");
                    }

                    const std::byte* formatData = fileData.data() + chunkDataOffset;
                    a_outWavData.format.formatTag = read_u16(formatData + 0);
                    a_outWavData.format.channelCount = read_u16(formatData + 2);
                    a_outWavData.format.samplesPerSecond = read_u32(formatData + 4);
                    a_outWavData.format.averageBytesPerSecond = read_u32(formatData + 8);
                    a_outWavData.format.blockAlign = read_u16(formatData + 12);
                    a_outWavData.format.bitsPerSample = read_u16(formatData + 14);
                    if (chunkSize > 16)
                    {
                        const size_t extraDataSize = static_cast<size_t>(chunkSize) - 16;
                        a_outWavData.extraData.resize(extraDataSize);
                        std::memcpy(
                            a_outWavData.extraData.data(),
                            formatData + 16,
                            extraDataSize);
                        a_outWavData.format.extraData =
                            std::span<const std::byte>(
                                a_outWavData.extraData.data(),
                                a_outWavData.extraData.size());
                    }
                    hasFormat = true;
                }
                else if (has_fourcc(chunkHeader, "data"))
                {
                    a_outWavData.audioData.resize(chunkSize);
                    std::memcpy(
                        a_outWavData.audioData.data(),
                        fileData.data() + chunkDataOffset,
                        chunkSize);
                    hasData = true;
                }

                offset = chunkDataEnd + (chunkSize & 1u);
            }

            if (!hasFormat || !hasData || a_outWavData.audioData.empty())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound wav file does not contain fmt/data chunks.");
            }

            if (a_outWavData.format.formatTag != 1 &&
                a_outWavData.format.formatTag != 3)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Sound cooker supports PCM or IEEE float wav only.");
            }

            return Result::ok();
        }

        [[nodiscard]] Result save_cuesound(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            const WavData& a_soundData)
        {
            const Core::IO::Path normalizedPath = a_filePath.normalize();
            if (normalizedPath.extension() != ".cuesound")
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound asset file extension must be .cuesound.");
            }

            CueSoundHeader header{};
            header.magic = k_cueSoundMagic;
            header.version = k_cueSoundVersion;
            header.formatTag = a_soundData.format.formatTag;
            header.channelCount = a_soundData.format.channelCount;
            header.samplesPerSecond = a_soundData.format.samplesPerSecond;
            header.averageBytesPerSecond =
                a_soundData.format.averageBytesPerSecond;
            header.blockAlign = a_soundData.format.blockAlign;
            header.bitsPerSample = a_soundData.format.bitsPerSample;
            header.extraDataSize =
                static_cast<uint32_t>(a_soundData.extraData.size());
            header.audioDataSize = a_soundData.audioData.size();

            const size_t headerSize = sizeof(CueSoundHeader);
            const size_t fileSize =
                headerSize +
                a_soundData.extraData.size() +
                a_soundData.audioData.size();
            std::vector<std::byte> fileData(fileSize);
            std::memcpy(fileData.data(), &header, sizeof(CueSoundHeader));

            size_t writeOffset = headerSize;
            if (!a_soundData.extraData.empty())
            {
                std::memcpy(
                    fileData.data() + writeOffset,
                    a_soundData.extraData.data(),
                    a_soundData.extraData.size());
                writeOffset += a_soundData.extraData.size();
            }

            std::memcpy(
                fileData.data() + writeOffset,
                a_soundData.audioData.data(),
                a_soundData.audioData.size());

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
        }

        [[nodiscard]] bool cooked_format_matches(
            Core::IO::IFileSystem& a_fileSystem,
            const Core::IO::Path& a_filePath,
            SoundCookFormat a_format)
        {
            std::vector<std::byte> fileData{};
            if (!a_fileSystem.read_all(a_filePath, &fileData))
            {
                return false;
            }
            if (fileData.size() < sizeof(CueSoundHeader))
            {
                return false;
            }

            CueSoundHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(header));
            if (header.magic != k_cueSoundMagic ||
                header.version != k_cueSoundVersion)
            {
                return false;
            }

            const uint16_t expectedFormat =
                a_format == SoundCookFormat::Adpcm
                    ? WAVE_FORMAT_ADPCM
                    : WAVE_FORMAT_PCM;
            return header.formatTag == expectedFormat;
        }
    }

    Result SoundCooker::cook_wav_to_cuesound(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath,
        SoundCookFormat a_format) noexcept
    {
        WavData wavData{};
        Result result = load_wav(a_fileSystem, a_sourcePath, wavData);
        if (!result)
        {
            return result;
        }

        WavData pcmData{};
        result = convert_to_pcm16(wavData, pcmData);
        if (!result)
        {
            return result;
        }

        if (a_format == SoundCookFormat::Pcm)
        {
            return save_cuesound(a_fileSystem, a_destinationPath, pcmData);
        }

        WavData adpcmData{};
        result = convert_pcm16_to_adpcm(pcmData, adpcmData);
        if (!result)
        {
            return result;
        }
        return save_cuesound(a_fileSystem, a_destinationPath, adpcmData);
    }

    Result SoundCooker::ensure_cuesound_is_up_to_date(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath,
        SoundCookFormat a_format) noexcept
    {
        bool cookedSoundExists = false;
        Result result = a_fileSystem.exists(a_destinationPath, &cookedSoundExists);
        if (!result)
        {
            return result;
        }

        bool shouldRecook = !cookedSoundExists;
        if (!shouldRecook)
        {
            Core::IO::FileStat sourceStat{};
            result = a_fileSystem.stat(a_sourcePath, &sourceStat);
            if (!result)
            {
                return result;
            }

            Core::IO::FileStat cookedStat{};
            result = a_fileSystem.stat(a_destinationPath, &cookedStat);
            if (!result)
            {
                return result;
            }

            shouldRecook = sourceStat.mtime_ns > cookedStat.mtime_ns;
            if (!shouldRecook)
            {
                shouldRecook =
                    !cooked_format_matches(
                        a_fileSystem,
                        a_destinationPath,
                        a_format);
            }
        }

        if (!shouldRecook)
        {
            return Result::ok();
        }

        return cook_wav_to_cuesound(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath,
            a_format);
    }
}
