#pragma once

#include <Cue/Project/Descriptor.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cue
{
class AssertContext;
class FilesystemRoot;
class RecentProjectRegistry;

/// @brief Registry が最後に確認した Project Locator の状態
enum class ProjectLocatorState : std::uint8_t
{
    Available,
    Missing,
    Moved
};

/// @brief User Workspace にだけ保存する ProjectId 基準の Recent Entry
class RecentProject final
{
  public:
    /// @brief 所有値を欠く Recent Entry を作らせないため既定構築を禁止する
    RecentProject() = delete;
    /// @brief 所有値の Allocation 例外を公開境界外へ出さないため Copy 構築を禁止する
    RecentProject(const RecentProject &) = delete;
    /// @brief 所有値の Allocation 例外を公開境界外へ出さないため Copy 代入を禁止する
    RecentProject &operator=(const RecentProject &) = delete;
    /// @brief Recent Entry の所有権を移動する
    RecentProject(RecentProject &&) noexcept = default;
    /// @brief Recent Entry を移動代入する
    RecentProject &operator=(RecentProject &&) noexcept = default;
    /// @brief Recent Entry の所有 Storage を解放する
    ~RecentProject() = default;

    /// @brief Path 変更で変化しない Project Identity を返す
    [[nodiscard]] const ProjectId &project_id() const noexcept;
    /// @brief Platform Composition が再 Open に使う UTF-8 Locator を返す
    [[nodiscard]] std::string_view locator() const noexcept;
    /// @brief 呼び出し側が供給した UTC Unix Millisecond を返す
    [[nodiscard]] std::uint64_t last_opened_milliseconds() const noexcept;
    /// @brief Entry が Pin 対象か返す
    [[nodiscard]] bool is_pinned() const noexcept;
    /// @brief Pin Entry 間の安定順序を返し、非 Pin では 0 を返す
    [[nodiscard]] std::uint64_t pin_order() const noexcept;
    /// @brief 非 Pin Entry の同時刻 Tie-break に使う安定登録順を返す
    [[nodiscard]] std::uint64_t registration_order() const noexcept;
    /// @brief Locator の最終確認状態を返す
    [[nodiscard]] ProjectLocatorState locator_state() const noexcept;

  private:
    friend class RecentProjectRegistry;
    friend Result<RecentProjectRegistry> parse_recent_project_registry(std::string_view,
                                                                        const AssertContext &) noexcept;

    /// @brief 検証済み Workspace Field を一つの所有 Entry へ束ねる
    RecentProject(ProjectId &&a_projectId, std::string &&a_locator, std::uint64_t a_lastOpenedMilliseconds,
                  bool a_isPinned, std::uint64_t a_pinOrder, std::uint64_t a_registrationOrder,
                  ProjectLocatorState a_locatorState) noexcept;

    ProjectId m_projectId;
    std::string m_locator;
    std::uint64_t m_lastOpenedMilliseconds;
    std::uint64_t m_pinOrder;
    std::uint64_t m_registrationOrder;
    ProjectLocatorState m_locatorState;
    bool m_isPinned;
};

/// @brief Project Folder を所有せず User Workspace の Recent 状態だけを管理する
///
/// 同一 Instance は Thread-safe ではなく、Mutation と保存は呼び出し側が同期する
/// Entry 削除は Registry からの除外だけを行い、Project Folder を削除しない
class RecentProjectRegistry final
{
  public:
    /// @brief 初回起動時の空 Registry を構築する
    RecentProjectRegistry() noexcept = default;
    /// @brief 所有 Entry の Allocation 例外を公開境界外へ出さないため Copy 構築を禁止する
    RecentProjectRegistry(const RecentProjectRegistry &) = delete;
    /// @brief 所有 Entry の Allocation 例外を公開境界外へ出さないため Copy 代入を禁止する
    RecentProjectRegistry &operator=(const RecentProjectRegistry &) = delete;
    /// @brief Registry の所有権を移動する
    RecentProjectRegistry(RecentProjectRegistry &&) noexcept = default;
    /// @brief Registry を移動代入する
    RecentProjectRegistry &operator=(RecentProjectRegistry &&) noexcept = default;
    /// @brief Registry の所有 Storage を解放する
    ~RecentProjectRegistry() = default;

    /// @brief Pin 順と最終 Open 順を反映した安定した非所有一覧を返す
    [[nodiscard]] std::span<const RecentProject> entries() const noexcept;

    /// @brief ProjectId を登録し、同一 Id の別 Locator を Duplicate として拒否する
    [[nodiscard]] Result<void> register_project(const ProjectDescriptor &a_descriptor, std::string_view a_locator,
                                                std::uint64_t a_lastOpenedMilliseconds,
                                                const AssertContext &a_assertContext) noexcept;
    /// @brief 明示的な移動確認後だけ既存 ProjectId を新 Locator へ再関連付けする
    [[nodiscard]] Result<void> reassociate_project(const ProjectDescriptor &a_descriptor, std::string_view a_locator,
                                                   std::uint64_t a_lastOpenedMilliseconds,
                                                   const AssertContext &a_assertContext) noexcept;
    /// @brief 欠損 Locator を Entry 削除へ変換せず Missing として保持する
    [[nodiscard]] Result<void> mark_project_missing(const ProjectId &a_projectId,
                                                    const AssertContext &a_assertContext) noexcept;
    /// @brief 再確認できた Locator を最終 Open 時刻を変えず Available へ戻す
    [[nodiscard]] Result<void> mark_project_available(const ProjectId &a_projectId,
                                                      const AssertContext &a_assertContext) noexcept;
    /// @brief ProjectId の Pin 状態を変更し、初回 Pin 順を安定して割り当てる
    [[nodiscard]] Result<void> set_project_pinned(const ProjectId &a_projectId, bool a_isPinned,
                                                  const AssertContext &a_assertContext) noexcept;
    /// @brief Pin Entry を現在の Pin 一覧内の 0-based 位置へ移動する
    [[nodiscard]] Result<void> move_pinned_project(const ProjectId &a_projectId, std::size_t a_targetIndex,
                                                   const AssertContext &a_assertContext) noexcept;
    /// @brief Recent 一覧から Entry だけを除外し、Project Folder へ IO を行わない
    [[nodiscard]] Result<void> remove_project(const ProjectId &a_projectId,
                                              const AssertContext &a_assertContext) noexcept;

  private:
    friend Result<RecentProjectRegistry> parse_recent_project_registry(std::string_view,
                                                                        const AssertContext &) noexcept;
    friend Result<std::string> serialize_recent_project_registry(const RecentProjectRegistry &,
                                                                 const AssertContext &) noexcept;

    /// @brief ProjectId が一致する Entry の Index を返す
    [[nodiscard]] std::size_t find_project(const ProjectId &a_projectId) const noexcept;
    /// @brief Locator が一致する Entry の Index を返す
    [[nodiscard]] std::size_t find_locator(std::string_view a_locator) const noexcept;
    /// @brief Pin と最終 Open の規則で一覧順を再構成する
    void sort_entries() noexcept;
    /// @brief 2 Entry の安定表示順を比較する
    [[nodiscard]] static bool comes_before(const RecentProject &a_left, const RecentProject &a_right) noexcept;
    /// @brief 永続化前後の Identity、Locator、順序不変条件を検証する
    [[nodiscard]] bool is_valid(const AssertContext &a_assertContext) const noexcept;

    std::vector<RecentProject> m_entries;
    std::uint64_t m_nextRegistrationOrder = 1U;
    std::uint64_t m_nextPinOrder = 1U;
};

/// @brief Version 付き User Workspace JSON を共通 Reader で解析する
[[nodiscard]] Result<RecentProjectRegistry> parse_recent_project_registry(
    std::string_view a_json, const AssertContext &a_assertContext) noexcept;

/// @brief Recent Registry を deterministic UTF-8 JSON へ直列化する
[[nodiscard]] Result<std::string> serialize_recent_project_registry(
    const RecentProjectRegistry &a_registry, const AssertContext &a_assertContext) noexcept;

/// @brief User Workspace Root の CueWorkspace.json を読込み、未作成なら空 Registry を返す
[[nodiscard]] Result<RecentProjectRegistry> load_recent_project_registry(
    FilesystemRoot &a_workspaceFilesystem, const AssertContext &a_assertContext) noexcept;

/// @brief User Workspace Root の CueWorkspace.json を Atomic に置換保存する
[[nodiscard]] Result<void> save_recent_project_registry(FilesystemRoot &a_workspaceFilesystem,
                                                        const RecentProjectRegistry &a_registry,
                                                        const AssertContext &a_assertContext) noexcept;
} // namespace cue
