#pragma once

#include <Cue/Foundation/Error.h>
#include <Cue/Foundation/Result.h>
#include <Cue/IO/Filesystem.h>
#include <Cue/Scene/SceneDocument.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cue::scene
{
/// @brief Scene File形式の現在対応Version
inline constexpr std::uint32_t k_currentSceneFormatVersion = 1U;

/// @brief 一つのScene File Versionを次Versionへ変換するFirst-party関数境界
using SceneMigrationFunction = Result<std::string> (*)(std::string_view, const AssertContext &) noexcept;

/// @brief 一つのComponent Schema Versionを次Versionへ変換するFirst-party関数境界
using ComponentMigrationFunction = Result<std::string> (*)(std::string_view, const AssertContext &) noexcept;

/// @brief NからN+1への連続Scene File Migrationだけを所有するRegistry
/// @details add_stepを呼ぶ構築期間は単一Threadまたは外部同期で直列化する。
/// 登録完了後はadd_stepを呼ばないImmutable状態として、複数Threadからmigrate、Parse、Saveへ共有できる。
/// 並行利用時は登録CallbackとCallbackが参照する状態も再入可能にするか、呼び出し側で外部同期する。
/// 登録関数の参照先はRegistryを利用する全処理の完了まで有効でなければならない。
class SceneMigrationRegistry final
{
  public:
    /// @brief 空のMigration Registryを生成する
    SceneMigrationRegistry() noexcept = default;
    /// @brief Migration登録集合を複製する
    SceneMigrationRegistry(const SceneMigrationRegistry &) = default;
    /// @brief Migration登録集合を複製代入する
    SceneMigrationRegistry &operator=(const SceneMigrationRegistry &) = default;
    /// @brief Migration登録集合を移動する
    SceneMigrationRegistry(SceneMigrationRegistry &&) noexcept = default;
    /// @brief Migration登録集合を移動代入する
    SceneMigrationRegistry &operator=(SceneMigrationRegistry &&) noexcept = default;
    /// @brief Migration登録集合を破棄する
    ~SceneMigrationRegistry() = default;

    /// @brief 一意なfromVersionへNからN+1の変換関数を登録する
    [[nodiscard]] Result<void> add_step(std::uint32_t a_fromVersion, SceneMigrationFunction a_function,
                                        const AssertContext &a_assertContext) noexcept;
    /// @brief 欠落Stepを許さず指定VersionまでMemory上で連続変換する
    [[nodiscard]] Result<std::string> migrate(std::string_view a_source, std::uint32_t a_fromVersion,
                                              std::uint32_t a_targetVersion,
                                              const AssertContext &a_assertContext) const noexcept;

  private:
    /// @brief 一つの連続Migration Stepを保持する
    struct Step final
    {
        std::uint32_t fromVersion;
        SceneMigrationFunction function;
    };

    std::vector<Step> m_steps;
};

/// @brief TypeごとのNからN+1への連続Component Field Migrationだけを所有するRegistry
/// @details add_stepを呼ぶ構築期間は単一Threadまたは外部同期で直列化する。
/// 登録完了後はadd_stepを呼ばないImmutable状態として、複数Threadからmigrate、Parse、Saveへ共有できる。
/// 並行利用時は登録CallbackとCallbackが参照する状態も再入可能にするか、呼び出し側で外部同期する。
/// 登録関数の参照先はRegistryを利用する全処理の完了まで有効でなければならない。
class ComponentMigrationRegistry final
{
  public:
    /// @brief 空のComponent Migration Registryを生成する
    ComponentMigrationRegistry() noexcept = default;
    /// @brief Component Migration登録集合を複製する
    ComponentMigrationRegistry(const ComponentMigrationRegistry &) = default;
    /// @brief Component Migration登録集合を複製代入する
    ComponentMigrationRegistry &operator=(const ComponentMigrationRegistry &) = default;
    /// @brief Component Migration登録集合を移動する
    ComponentMigrationRegistry(ComponentMigrationRegistry &&) noexcept = default;
    /// @brief Component Migration登録集合を移動代入する
    ComponentMigrationRegistry &operator=(ComponentMigrationRegistry &&) noexcept = default;
    /// @brief Component Migration登録集合を破棄する
    ~ComponentMigrationRegistry() = default;

    /// @brief TypeIdと一意なfromVersionへNからN+1のField変換関数を登録する
    [[nodiscard]] Result<void> add_step(schema::TypeId a_typeId, std::uint32_t a_fromVersion,
                                        ComponentMigrationFunction a_function,
                                        const AssertContext &a_assertContext) noexcept;
    /// @brief 欠落Stepを許さず指定TypeのField Arrayを現在VersionまでMemory上で連続変換する
    [[nodiscard]] Result<std::string> migrate(schema::TypeId a_typeId, std::string_view a_fieldsJson,
                                              std::uint32_t a_fromVersion, std::uint32_t a_targetVersion,
                                              const AssertContext &a_assertContext) const noexcept;

  private:
    /// @brief 一つのTypeとVersionに対応する連続Migration Stepを保持する
    struct Step final
    {
        schema::TypeId typeId;
        std::uint32_t fromVersion;
        ComponentMigrationFunction function;
    };

    std::vector<Step> m_steps;
};

/// @brief 完全Parse済みDocumentとSource Version状態を所有するLoad結果
class SceneLoadResult final
{
  public:
    /// @brief Load結果の一意Document所有を保つためCopy構築を禁止する
    SceneLoadResult(const SceneLoadResult &) = delete;
    /// @brief Load結果の一意Document所有を保つためCopy代入を禁止する
    SceneLoadResult &operator=(const SceneLoadResult &) = delete;
    /// @brief Load結果を移動する
    SceneLoadResult(SceneLoadResult &&) noexcept = default;
    /// @brief Load結果を移動代入する
    SceneLoadResult &operator=(SceneLoadResult &&) noexcept = default;
    /// @brief Load結果所有値を破棄する
    ~SceneLoadResult() = default;

    /// @brief 完全検証済みSceneDocumentを返す
    [[nodiscard]] SceneDocument &document() noexcept;
    /// @brief 完全検証済みSceneDocumentを返す
    [[nodiscard]] const SceneDocument &document() const noexcept;
    /// @brief 読込元FileのFormat Versionを返す
    [[nodiscard]] std::uint32_t source_format_version() const noexcept;
    /// @brief 明示Saveまで元Fileを維持すべきMigration済み状態か返す
    [[nodiscard]] bool migration_required() const noexcept;

  private:
    friend Result<SceneLoadResult> parse_scene_document(std::string_view, const schema::SchemaRegistry &,
                                                        const ComponentValueSchemaRegistry &,
                                                        const SceneMigrationRegistry &,
                                                        const ComponentMigrationRegistry &,
                                                        const AssertContext &) noexcept;

    /// @brief 検証済みDocumentと元Versionを束ねる
    SceneLoadResult(SceneDocument a_document, std::uint32_t a_sourceFormatVersion) noexcept;

    SceneDocument m_document;
    std::uint32_t m_sourceFormatVersion;
};

/// @brief Atomic SaveがFile公開境界のどこまで到達したか表す
enum class SceneSaveStatus : std::uint8_t
{
    Committed,
    NotPublished,
    PublishedButDurabilityUnknown,
    PublishedButVerificationFailed
};

/// @brief Save公開状態と失敗診断を同時に所有する結果
class SceneSaveOutcome final
{
  public:
    /// @brief Move-only Errorを含むためCopy構築を禁止する
    SceneSaveOutcome(const SceneSaveOutcome &) = delete;
    /// @brief Move-only Errorを含むためCopy代入を禁止する
    SceneSaveOutcome &operator=(const SceneSaveOutcome &) = delete;
    /// @brief Save結果を移動する
    SceneSaveOutcome(SceneSaveOutcome &&) noexcept = default;
    /// @brief Save結果を移動代入する
    SceneSaveOutcome &operator=(SceneSaveOutcome &&) noexcept = default;
    /// @brief Save結果所有値を破棄する
    ~SceneSaveOutcome() = default;

    /// @brief Saveの公開状態を返す
    [[nodiscard]] SceneSaveStatus status() const noexcept;
    /// @brief 失敗診断またはCommittedならnullptrを返す
    [[nodiscard]] const Error *try_error() const noexcept;

    /// @brief 完全Commit済み結果を生成する
    [[nodiscard]] static SceneSaveOutcome committed() noexcept;
    /// @brief 本文未公開の失敗結果を生成する
    [[nodiscard]] static SceneSaveOutcome not_published(Error a_error) noexcept;
    /// @brief 本文公開済みだがDurability不明の結果を生成する
    [[nodiscard]] static SceneSaveOutcome durability_unknown(Error a_error) noexcept;
    /// @brief 本文Commit後の再読込比較だけが失敗した結果を生成する
    [[nodiscard]] static SceneSaveOutcome verification_failed(Error a_error) noexcept;

  private:
    /// @brief 公開状態と任意診断を束ねる
    SceneSaveOutcome(SceneSaveStatus a_status, std::optional<Error> a_error) noexcept;

    SceneSaveStatus m_status;
    std::optional<Error> m_error;
};

/// @brief 現行Versionの固定順JSONへSceneDocumentをSerializeする
[[nodiscard]] Result<std::string> serialize_scene_document(const SceneDocument &a_document,
                                                           const AssertContext &a_assertContext) noexcept;

/// @brief JSONをMigration後に現在Schemaと照合して新しいSceneDocumentへParseする
/// @details File Sizeまたは意味別Resource上限超過を診断付きで拒否し、入力と既存Documentを変更しない
/// @param a_assertContext 返却SceneDocumentより長く生存する非所有診断Context
[[nodiscard]] Result<SceneLoadResult> parse_scene_document(std::string_view a_json,
                                                           const schema::SchemaRegistry &a_schemaRegistry,
                                                           const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                                           const SceneMigrationRegistry &a_migrationRegistry,
                                                           const ComponentMigrationRegistry &a_componentMigrations,
                                                           const AssertContext &a_assertContext) noexcept;

/// @brief Root相対Locatorから上限付きでSceneを読込み完全Parseする
/// @param a_assertContext 返却SceneDocumentより長く生存する非所有診断Context
[[nodiscard]] Result<SceneLoadResult> load_scene_document(FilesystemRoot &a_filesystem, const RelativePath &a_path,
                                                          const schema::SchemaRegistry &a_schemaRegistry,
                                                          const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                                          const SceneMigrationRegistry &a_migrationRegistry,
                                                          const ComponentMigrationRegistry &a_componentMigrations,
                                                          const AssertContext &a_assertContext) noexcept;

/// @brief CandidateをParse-backしBackup作成後に本文をAtomic置換する
/// @pre 呼び出し側が処理完了まで本文Pathと`.backup` Pathへの排他的な書込み所有権を保証する
[[nodiscard]] SceneSaveOutcome save_scene_document(FilesystemRoot &a_filesystem, const RelativePath &a_path,
                                                   const SceneDocument &a_document,
                                                   const schema::SchemaRegistry &a_schemaRegistry,
                                                   const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
                                                   const SceneMigrationRegistry &a_migrationRegistry,
                                                   const ComponentMigrationRegistry &a_componentMigrations,
                                                   const AssertContext &a_assertContext) noexcept;
/// @brief Lease保持中に期待Fingerprintを再検査し、一致する場合だけSceneをAtomic置換する
[[nodiscard]] SceneSaveOutcome save_scene_document_if_unchanged(
    FilesystemRoot &a_filesystem, FileWriteLease &a_lease, const RelativePath &a_path, FileFingerprint a_expected,
    const SceneDocument &a_document, const schema::SchemaRegistry &a_schemaRegistry,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry, const SceneMigrationRegistry &a_migrationRegistry,
    const ComponentMigrationRegistry &a_componentMigrations, const AssertContext &a_assertContext) noexcept;
} // namespace cue::scene
