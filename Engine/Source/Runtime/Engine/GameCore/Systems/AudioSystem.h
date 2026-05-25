#pragma once

// === Base includes ===
#include <Result.h>

// === Audio includes ===
#include <Audio.h>

// === Core includes ===
#include <IO/IFileSystem.h>
#include <IO/Path.h>

// === ECS includes ===
#include <ECSManager.h>

// === Engine includes ===
#include <GameCore/Components.h>
#include <asset/SoundAssetFormat.h>

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <vector>

namespace Cue::ECS
{
    class AudioSystem final : public ECSManager::System<AudioSourceComponent>
    {
    public:
        AudioSystem(Core::IO::IFileSystem* a_fileSystem,
            Audio::IBackend* a_audioBackend,
            Audio::AudioDeviceHandle a_deviceHandle,
            const Core::IO::Path& a_assetRootPath)
            : ECSManager::System<AudioSourceComponent>(
                [this](Entity a_entity, AudioSourceComponent& a_audioSource,
                    const UpdateContext& a_context)
                {
                    update_component(a_entity, a_audioSource, a_context);
                },
                [this](Entity a_entity, AudioSourceComponent& a_audioSource,
                    const InitializeContext& a_context)
                {
                    initialize_component(a_entity, a_audioSource, a_context);
                },
                [this](Entity a_entity, AudioSourceComponent& a_audioSource,
                    const FinalizeContext& a_context)
                {
                    finalize_component(a_entity, a_audioSource, a_context);
                }),
            m_fileSystem(a_fileSystem),
            m_audioBackend(a_audioBackend),
            m_deviceHandle(a_deviceHandle),
            m_assetRootPath(a_assetRootPath)
        {}

    private:
        struct SoundData final
        {
            Audio::AudioFormatDesc format{};
            std::vector<std::byte> extraData{};
            std::vector<std::byte> audioData{};
        };

        [[nodiscard]] static uint16_t read_u16(
            const std::byte* a_data) noexcept
        {
            return static_cast<uint16_t>(
                static_cast<uint16_t>(std::to_integer<uint8_t>(a_data[0])) |
                (static_cast<uint16_t>(
                    std::to_integer<uint8_t>(a_data[1])) << 8));
        }

        [[nodiscard]] static uint32_t loop_length_samples(
            const Audio::AudioFormatDesc& a_format,
            size_t a_audioDataSize) noexcept
        {
            if (a_format.blockAlign == 0)
            {
                return 0;
            }

            const uint32_t blockCount = static_cast<uint32_t>(
                a_audioDataSize / a_format.blockAlign);
            if (a_format.formatTag == 2 &&
                a_format.extraData.size() >= sizeof(uint16_t))
            {
                return blockCount *
                    static_cast<uint32_t>(
                        read_u16(a_format.extraData.data()));
            }

            return blockCount;
        }

        void initialize_component(Entity a_entity,
            AudioSourceComponent& a_audioSource,
            const InitializeContext& a_context)
        {
            a_entity;
            a_context;
            a_audioSource.sourceHandle = {};
            a_audioSource.isPlaying = false;
            a_audioSource.playRequested = false;
            a_audioSource.stopRequested = false;
            a_audioSource.hasStarted = false;
        }

        void update_component(Entity a_entity,
            AudioSourceComponent& a_audioSource,
            const UpdateContext& a_context)
        {
            a_entity;
            if (!a_audioSource.is_active())
            {
                const Result result = stop_and_destroy(a_audioSource);
                if (!result)
                {
                    return;
                }
                return;
            }

            if (a_audioSource.stopRequested)
            {
                a_audioSource.stopRequested = false;
                const Result result = stop_and_destroy(a_audioSource);
                if (!result)
                {
                    return;
                }
            }

            const bool canAutoPlay = a_context.deltaTime > 0.0f;
            const bool shouldAutoPlay =
                canAutoPlay &&
                a_audioSource.playOnStart &&
                !a_audioSource.hasStarted;
            if (a_audioSource.playRequested || shouldAutoPlay)
            {
                a_audioSource.playRequested = false;
                a_audioSource.hasStarted = true;
                (void)play(a_audioSource);
            }

            if (a_audioSource.isPlaying &&
                a_audioSource.sourceHandle.valid() &&
                m_audioBackend != nullptr)
            {
                (void)m_audioBackend->set_source_volume(
                    a_audioSource.sourceHandle,
                    (std::max)(0.0f, a_audioSource.volume));
            }
        }

        void finalize_component(Entity a_entity,
            AudioSourceComponent& a_audioSource,
            const FinalizeContext& a_context)
        {
            a_entity;
            a_context;
            const Result result = stop_and_destroy(a_audioSource);
            if (!result)
            {
                return;
            }
            a_audioSource.hasStarted = false;
        }

        [[nodiscard]] Result play(AudioSourceComponent& a_audioSource)
        {
            if (m_audioBackend == nullptr || m_fileSystem == nullptr)
            {
                return Result::fail(Code::InvalidState, Severity::Error,
                    "AudioSystem is not initialized.");
            }
            if (a_audioSource.fileName.empty())
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "AudioSource fileName is empty.");
            }

            Result result = stop_and_destroy(a_audioSource);
            if (!result)
            {
                return result;
            }

            const Core::IO::Path filePath =
                resolve_audio_source_path(a_audioSource.fileName);
            if (filePath.extension() != ".cuesound")
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "AudioSource supports cuesound files only.");
            }

            SoundData soundData{};
            result = load_cuesound(filePath, soundData);
            if (!result)
            {
                return result;
            }

            Audio::AudioSourceDesc sourceDesc{};
            sourceDesc.format = soundData.format;
            sourceDesc.audioData = std::span<const std::byte>(
                soundData.audioData.data(),
                soundData.audioData.size());
            sourceDesc.volume = (std::max)(0.0f, a_audioSource.volume);
            if (a_audioSource.loop)
            {
                constexpr uint32_t k_loopInfinite = 255u;
                sourceDesc.loopCount = k_loopInfinite;
                if (sourceDesc.format.blockAlign > 0)
                {
                    sourceDesc.loopLength = loop_length_samples(
                        sourceDesc.format,
                        soundData.audioData.size());
                }
            }

            result = m_audioBackend->create_source(
                m_deviceHandle,
                sourceDesc,
                a_audioSource.sourceHandle);
            if (!result)
            {
                return result;
            }

            result = m_audioBackend->play_source(a_audioSource.sourceHandle);
            if (!result)
            {
                (void)m_audioBackend->destroy_source(a_audioSource.sourceHandle);
                a_audioSource.sourceHandle = {};
                a_audioSource.isPlaying = false;
                return result;
            }

            a_audioSource.isPlaying = true;
            return Result::ok();
        }

        [[nodiscard]] Result stop_and_destroy(AudioSourceComponent& a_audioSource)
        {
            if (m_audioBackend == nullptr)
            {
                a_audioSource.sourceHandle = {};
                a_audioSource.isPlaying = false;
                return Result::ok();
            }

            if (!a_audioSource.sourceHandle.valid())
            {
                a_audioSource.isPlaying = false;
                return Result::ok();
            }

            Result result = m_audioBackend->stop_source(a_audioSource.sourceHandle);
            if (!result)
            {
                return result;
            }

            result = m_audioBackend->destroy_source(a_audioSource.sourceHandle);
            if (!result)
            {
                return result;
            }

            a_audioSource.sourceHandle = {};
            a_audioSource.isPlaying = false;
            return Result::ok();
        }

        [[nodiscard]] Core::IO::Path resolve_audio_source_path(
            const std::string& a_fileName) const
        {
            Core::IO::Path filePath(a_fileName);
            if (filePath.is_absolute())
            {
                return filePath.normalize();
            }

            if (m_assetRootPath.is_empty())
            {
                return filePath.normalize();
            }

            if (a_fileName.find('/') != std::string::npos ||
                a_fileName.find('\\') != std::string::npos)
            {
                return Core::IO::Path::join(m_assetRootPath, filePath).normalize();
            }

            return Core::IO::Path::join(
                Core::IO::Path::join(m_assetRootPath, Core::IO::Path("Sounds")),
                filePath).normalize();
        }

        [[nodiscard]] Result load_cuesound(
            const Core::IO::Path& a_filePath,
            SoundData& a_outSoundData) const
        {
            static_assert(std::is_trivially_copyable_v<CueSoundHeader>);

            a_outSoundData = {};
            std::vector<std::byte> fileData{};
            Result result = m_fileSystem->read_all(a_filePath, &fileData);
            if (!result)
            {
                return result;
            }

            if (fileData.size() < sizeof(CueSoundHeader))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound asset file is too small.");
            }

            CueSoundHeader header{};
            std::memcpy(&header, fileData.data(), sizeof(CueSoundHeader));
            if (header.magic != k_cueSoundMagic ||
                header.version != k_cueSoundVersion)
            {
                return Result::fail(Code::Unsupported, Severity::Error,
                    "Sound asset version is not supported.");
            }
            if (header.channelCount == 0 ||
                header.samplesPerSecond == 0 ||
                header.blockAlign == 0 ||
                header.bitsPerSample == 0 ||
                header.audioDataSize == 0)
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound asset format is invalid.");
            }

            const uint64_t expectedSize =
                sizeof(CueSoundHeader) +
                static_cast<uint64_t>(header.extraDataSize) +
                header.audioDataSize;
            if (expectedSize > static_cast<uint64_t>(fileData.size()))
            {
                return Result::fail(Code::InvalidArgument, Severity::Error,
                    "Sound asset payload is invalid.");
            }

            a_outSoundData.format.formatTag = header.formatTag;
            a_outSoundData.format.channelCount = header.channelCount;
            a_outSoundData.format.samplesPerSecond = header.samplesPerSecond;
            a_outSoundData.format.averageBytesPerSecond =
                header.averageBytesPerSecond;
            a_outSoundData.format.blockAlign = header.blockAlign;
            a_outSoundData.format.bitsPerSample = header.bitsPerSample;

            size_t readOffset = sizeof(CueSoundHeader);
            if (header.extraDataSize > 0)
            {
                a_outSoundData.extraData.resize(header.extraDataSize);
                std::memcpy(
                    a_outSoundData.extraData.data(),
                    fileData.data() + readOffset,
                    header.extraDataSize);
                readOffset += header.extraDataSize;
                a_outSoundData.format.extraData =
                    std::span<const std::byte>(
                        a_outSoundData.extraData.data(),
                        a_outSoundData.extraData.size());
            }

            a_outSoundData.audioData.resize(
                static_cast<size_t>(header.audioDataSize));
            std::memcpy(
                a_outSoundData.audioData.data(),
                fileData.data() + readOffset,
                a_outSoundData.audioData.size());
            return Result::ok();
        }

    private:
        Core::IO::IFileSystem* m_fileSystem = nullptr;
        Audio::IBackend* m_audioBackend = nullptr;
        Audio::AudioDeviceHandle m_deviceHandle{};
        const Core::IO::Path& m_assetRootPath;
    };
}
