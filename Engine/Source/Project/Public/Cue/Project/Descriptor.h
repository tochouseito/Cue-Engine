#pragma once

#include <Cue/Foundation/Result.h>
#include <Cue/IO/RelativePath.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace cue
{
class AssertContext;
class FilesystemRoot;

/// @brief Project 移動や表示名変更でも変化しない UUID Version 4 Identity
class ProjectId final
{
  public:
    /// @brief 未検証または Nil の Identity を作らせないため既定構築を禁止する
    ProjectId() = delete;
    /// @brief 検証済み Identity を複製する
    ProjectId(const ProjectId &) = default;
    /// @brief 検証済み Identity を複製代入する
    ProjectId &operator=(const ProjectId &) = default;
    /// @brief 検証済み Identity の所有権を移動する
    ProjectId(ProjectId &&) noexcept = default;
    /// @brief 検証済み Identity を移動代入する
    ProjectId &operator=(ProjectId &&) noexcept = default;
    /// @brief Identity の所有 Storage を解放する
    ~ProjectId() = default;

    /// @brief lowercase 8-4-4-4-12 UUID Version 4 を検証して所有値を返す
    [[nodiscard]] static Result<ProjectId> parse(std::string_view a_text,
                                                 const AssertContext &a_assertContext) noexcept;

    /// @brief Descriptor へ保存する canonical UUID 文字列を返す
    [[nodiscard]] std::string_view text() const noexcept;

    /// @brief UUID の全 128-bit が一致するか比較する
    [[nodiscard]] bool operator==(const ProjectId &) const noexcept = default;

  private:
    /// @brief 検証済み canonical UUID だけから Identity を構築する
    explicit ProjectId(std::string &&a_text) noexcept;

    std::string m_text;
};

/// @brief CueEngine Release Version の比較可能な major.minor.patch 値
struct EngineVersion final
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;

    /// @brief 3 要素を辞書順で比較する
    [[nodiscard]] auto operator<=>(const EngineVersion &) const noexcept = default;
};

/// @brief Project が許容する CueEngine Version の半開区間
struct EngineCompatibility final
{
    EngineVersion minimum;
    std::optional<EngineVersion> maximumExclusive;

    /// @brief Version 範囲の全要素が一致するか比較する
    [[nodiscard]] bool operator==(const EngineCompatibility &) const noexcept = default;
};

/// @brief Project Root 配下で役割が重ならない 4 種の Directory
struct ProjectRoots final
{
    RelativePath sourceAssets;
    RelativePath runtimeAssets;
    RelativePath generated;
    RelativePath saved;
};

/// @brief 検証済み Project Descriptor v1 の所有 Model
class ProjectDescriptor final
{
  public:
    /// @brief 必須値を欠く Descriptor を作らせないため既定構築を禁止する
    ProjectDescriptor() = delete;
    /// @brief 検証済み Descriptor を複製する
    ProjectDescriptor(const ProjectDescriptor &) = default;
    /// @brief 検証済み Descriptor を複製代入する
    ProjectDescriptor &operator=(const ProjectDescriptor &) = default;
    /// @brief Descriptor の所有権を移動する
    ProjectDescriptor(ProjectDescriptor &&) noexcept = default;
    /// @brief Descriptor を移動代入する
    ProjectDescriptor &operator=(ProjectDescriptor &&) noexcept = default;
    /// @brief Descriptor の所有 Storage を解放する
    ~ProjectDescriptor() = default;

    /// @brief 対応する Descriptor Format Version を返す
    [[nodiscard]] std::uint32_t schema_version() const noexcept;
    /// @brief Stable Project Identity を返す
    [[nodiscard]] const ProjectId &project_id() const noexcept;
    /// @brief UI 表示専用の Project 名を返す
    [[nodiscard]] std::string_view display_name() const noexcept;
    /// @brief CueEngine Version 互換範囲を返す
    [[nodiscard]] const EngineCompatibility &engine_compatibility() const noexcept;
    /// @brief Project Root 配下の役割別 Directory を返す
    [[nodiscard]] const ProjectRoots &roots() const noexcept;
    /// @brief 未知 Extension を意味解釈せず保持する canonical JSON Object を返す
    [[nodiscard]] std::string_view extensions_json() const noexcept;

    /// @brief 永続化対象の全論理値が一致するか比較する
    [[nodiscard]] bool equivalent_to(const ProjectDescriptor &a_other) const noexcept;

  private:
    friend Result<ProjectDescriptor> parse_project_descriptor(std::string_view, const AssertContext &) noexcept;

    /// @brief Parser が検証した Descriptor v1 の所有値を束ねる
    ProjectDescriptor(ProjectId &&a_projectId, std::string &&a_displayName, EngineCompatibility a_engineCompatibility,
                      ProjectRoots &&a_roots, std::string &&a_extensionsJson) noexcept;

    ProjectId m_projectId;
    std::string m_displayName;
    EngineCompatibility m_engineCompatibility;
    ProjectRoots m_roots;
    std::string m_extensionsJson;
};

/// @brief UTF-8 JSON を一度だけ解析し、検証済み Descriptor v1 を構築する
[[nodiscard]] Result<ProjectDescriptor> parse_project_descriptor(std::string_view a_json,
                                                                 const AssertContext &a_assertContext) noexcept;

/// @brief 所有 Model が Descriptor v1 の全不変条件を満たすか再検証する
[[nodiscard]] Result<void> validate_project_descriptor(const ProjectDescriptor &a_descriptor,
                                                       const AssertContext &a_assertContext) noexcept;

/// @brief 検証済み Descriptor を deterministic UTF-8 JSON へ直列化する
[[nodiscard]] Result<std::string> serialize_project_descriptor(const ProjectDescriptor &a_descriptor,
                                                               const AssertContext &a_assertContext) noexcept;

/// @brief Project Root の CueProject.json を上限付きで読み込んで解析する
[[nodiscard]] Result<ProjectDescriptor> load_project_descriptor(FilesystemRoot &a_filesystem,
                                                                const AssertContext &a_assertContext) noexcept;

/// @brief Project Root の CueProject.json を Atomic に置換保存する
[[nodiscard]] Result<void> save_project_descriptor(FilesystemRoot &a_filesystem, const ProjectDescriptor &a_descriptor,
                                                   const AssertContext &a_assertContext) noexcept;
} // namespace cue
