#include "SoundCooker.h"

// === Audio Includes ===
#include <Audio.h>

// === Engine Includes ===
#include <asset/SoundAssetFormat.h>

// === C++ Includes ===
#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

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
            const WavData& a_wavData)
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
            header.formatTag = a_wavData.format.formatTag;
            header.channelCount = a_wavData.format.channelCount;
            header.samplesPerSecond = a_wavData.format.samplesPerSecond;
            header.averageBytesPerSecond = a_wavData.format.averageBytesPerSecond;
            header.blockAlign = a_wavData.format.blockAlign;
            header.bitsPerSample = a_wavData.format.bitsPerSample;
            header.extraDataSize = static_cast<uint32_t>(a_wavData.extraData.size());
            header.audioDataSize = a_wavData.audioData.size();

            const size_t headerSize = sizeof(CueSoundHeader);
            const size_t fileSize =
                headerSize +
                a_wavData.extraData.size() +
                a_wavData.audioData.size();
            std::vector<std::byte> fileData(fileSize);
            std::memcpy(fileData.data(), &header, sizeof(CueSoundHeader));

            size_t writeOffset = headerSize;
            if (!a_wavData.extraData.empty())
            {
                std::memcpy(
                    fileData.data() + writeOffset,
                    a_wavData.extraData.data(),
                    a_wavData.extraData.size());
                writeOffset += a_wavData.extraData.size();
            }

            std::memcpy(
                fileData.data() + writeOffset,
                a_wavData.audioData.data(),
                a_wavData.audioData.size());

            return a_fileSystem.write_all(
                normalizedPath,
                std::span<const std::byte>(fileData.data(), fileData.size()),
                true);
        }
    }

    Result SoundCooker::cook_wav_to_cuesound(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
    {
        WavData wavData{};
        Result result = load_wav(a_fileSystem, a_sourcePath, wavData);
        if (!result)
        {
            return result;
        }

        return save_cuesound(a_fileSystem, a_destinationPath, wavData);
    }

    Result SoundCooker::ensure_cuesound_is_up_to_date(
        Core::IO::IFileSystem& a_fileSystem,
        const Core::IO::Path& a_sourcePath,
        const Core::IO::Path& a_destinationPath) noexcept
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
        }

        if (!shouldRecook)
        {
            return Result::ok();
        }

        return cook_wav_to_cuesound(
            a_fileSystem,
            a_sourcePath,
            a_destinationPath);
    }
}
