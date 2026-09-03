#pragma once

#include <Cue/Scene/Identity.h>
#include <Cue/Schema/Registry.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace cue::scene
{
class SceneDocumentSerializationAccess;

/// @brief Scene File内の復号済み一String Valueが所有できる最大Byte数
inline constexpr std::size_t k_maximumSceneStringBytes = 256U * 1024U;
/// @brief Scene File全体または一つのOpaque JSONが所有できる最大Byte数
inline constexpr std::size_t k_maximumSceneBytes = 16U * 1024U * 1024U;
/// @brief Scene File内の一ObjectまたはArrayが所有できる最大要素数
inline constexpr std::size_t k_maximumSceneContainerElements = 4096U;
/// @brief 一つのSceneDocumentが所有できるObject数上限
inline constexpr std::size_t k_maximumSceneObjectCount = k_maximumSceneContainerElements;
/// @brief 一つのScene Objectが所有できるComponent数上限
inline constexpr std::size_t k_maximumSceneComponentsPerObject = k_maximumSceneContainerElements;
/// @brief 一つのScene Componentが所有できる既知・未知Field合計数上限
inline constexpr std::size_t k_maximumSceneFieldsPerComponent = k_maximumSceneContainerElements;
/// @brief Scene File内のRootから数えるJSON Container最大Nesting Depth
inline constexpr std::size_t k_maximumSceneNestingDepth = 64U;
/// @brief JSON Tree全体が所有できるValueとObject Memberの合計上限
inline constexpr std::size_t k_maximumSceneJsonNodes = 256U * 1024U;

/// @brief Scene Wire Dataで解釈可能なField Valueの意味Kind
enum class FieldValueKind : std::uint8_t
{
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    String,
    AssetReference
};

/// @brief Asset DatabaseのIdentity表現を決めずStable参照Tokenを保持する値
class AssetReferenceValue final
{
  public:
    /// @brief 空でないStable Asset参照Tokenを検証して返す
    [[nodiscard]] static Result<AssetReferenceValue> create(
        std::string_view a_token,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Asset Databaseが解決するOpaque Stable Tokenを返す
    [[nodiscard]] std::string_view token() const noexcept;
    /// @brief Asset参照Tokenが一致するか比較する
    [[nodiscard]] bool operator==(const AssetReferenceValue &) const noexcept = default;

  private:
    /// @brief 検証済みAsset参照Tokenを所有する
    explicit AssetReferenceValue(std::string a_token) noexcept;

    std::string m_token;
};

/// @brief Memory Imageに依存しない一つの型付きAuthoring Field Value
class FieldValue final
{
  public:
    /// @brief Boolean Field Valueを生成する
    [[nodiscard]] static FieldValue boolean(bool a_value) noexcept;
    /// @brief Signed Integer Field Valueを生成する
    [[nodiscard]] static FieldValue signed_integer(std::int64_t a_value) noexcept;
    /// @brief Unsigned Integer Field Valueを生成する
    [[nodiscard]] static FieldValue unsigned_integer(std::uint64_t a_value) noexcept;
    /// @brief 有限Floating Point Field Valueを検証して生成する
    [[nodiscard]] static Result<FieldValue> floating_point(
        double a_value, const AssertContext &a_assertContext) noexcept;
    /// @brief UTF-8 Wire Stringとして保持するField Valueを生成する
    [[nodiscard]] static Result<FieldValue> string(
        std::string_view a_value, const AssertContext &a_assertContext) noexcept;
    /// @brief Asset参照Field Valueを生成する
    [[nodiscard]] static FieldValue asset_reference(
        AssetReferenceValue a_value) noexcept;

    /// @brief Valueの明示Kindを返す
    [[nodiscard]] FieldValueKind kind() const noexcept;
    /// @brief Boolean Valueまたはnullptrを返す
    [[nodiscard]] const bool *try_boolean() const noexcept;
    /// @brief Signed Integer Valueまたはnullptrを返す
    [[nodiscard]] const std::int64_t *try_signed_integer() const noexcept;
    /// @brief Unsigned Integer Valueまたはnullptrを返す
    [[nodiscard]] const std::uint64_t *try_unsigned_integer() const noexcept;
    /// @brief Floating Point Valueまたはnullptrを返す
    [[nodiscard]] const double *try_floating_point() const noexcept;
    /// @brief String Valueまたはnullptrを返す
    [[nodiscard]] const std::string *try_string() const noexcept;
    /// @brief Asset Reference Valueまたはnullptrを返す
    [[nodiscard]] const AssetReferenceValue *try_asset_reference() const noexcept;

    /// @brief Field ValueのKindと内容が一致するか比較する
    [[nodiscard]] bool operator==(const FieldValue &) const noexcept = default;

  private:
    using Storage = std::variant<bool, std::int64_t, std::uint64_t, double,
                                 std::string, AssetReferenceValue>;

    /// @brief 一つの検証済みValue Alternativeを所有する
    explicit FieldValue(Storage a_storage) noexcept;

    Storage m_storage;
};

/// @brief TypeごとのFieldIdへ期待するScene Value Kindを結び付ける
struct FieldKindBinding final
{
    schema::FieldId id;
    FieldValueKind kind;
};

/// @brief 一つのTypeIdとSchemaVersionにField Value Kindを不変結合する
class ComponentValueSchema final
{
  public:
    /// @brief Stable Component Type Identityを返す
    [[nodiscard]] schema::TypeId type_id() const noexcept;
    /// @brief 対応するSchema Versionを返す
    [[nodiscard]] schema::SchemaVersion version() const noexcept;
    /// @brief Stable FieldId順のValue Kind Bindingを返す
    [[nodiscard]] std::span<const FieldKindBinding> field_kinds() const noexcept;

  private:
    friend class ComponentValueSchemaRegistry;
    friend Result<ComponentValueSchema> create_component_value_schema(
        schema::TypeId, schema::SchemaVersion,
        std::vector<FieldKindBinding>, const schema::SchemaRegistry &,
        const AssertContext &) noexcept;

    /// @brief 検証済みType、Version、Field Kind集合を所有する
    ComponentValueSchema(schema::TypeId a_typeId,
                         schema::SchemaVersion a_version,
                         std::vector<FieldKindBinding> a_fieldKinds,
                         const schema::SchemaRegistry &a_schemaRegistry) noexcept;

    schema::TypeId m_typeId;
    schema::SchemaVersion m_version;
    std::vector<FieldKindBinding> m_fieldKinds;
    schema::SchemaRegistryGenerationToken m_generationToken;
};

/// @brief TypeIdごとに一つのComponent Value Schemaを所有するImmutable Registry
class ComponentValueSchemaRegistry final
{
  public:
    /// @brief 無効な未検証Registryを作らせないため既定構築を禁止する
    ComponentValueSchemaRegistry() = delete;
    /// @brief Immutable Schema集合を複製する
    ComponentValueSchemaRegistry(const ComponentValueSchemaRegistry &) = default;
    /// @brief Immutable Schema集合を複製代入する
    ComponentValueSchemaRegistry &operator=(const ComponentValueSchemaRegistry &) = default;
    /// @brief Immutable Schema集合を移動する
    ComponentValueSchemaRegistry(ComponentValueSchemaRegistry &&) noexcept = default;
    /// @brief Immutable Schema集合を移動代入する
    ComponentValueSchemaRegistry &operator=(ComponentValueSchemaRegistry &&) noexcept = default;
    /// @brief Immutable Schema集合を破棄する
    ~ComponentValueSchemaRegistry() = default;

    /// @brief TypeId重複を拒否して一つのM10 Registryへ結び付くImmutable Registryを生成する
    /// @param a_schemaRegistry OwnerがComponentValueSchemaRegistryより長く生存させるM10 Registry
    [[nodiscard]] static Result<ComponentValueSchemaRegistry> create(
        std::vector<ComponentValueSchema> a_schemas,
        const schema::SchemaRegistry &a_schemaRegistry,
        const AssertContext &a_assertContext) noexcept;

    /// @brief TypeIdに対応する不変Value Schemaまたはnullptrを返す
    [[nodiscard]] const ComponentValueSchema *find(
        schema::TypeId a_typeId) const noexcept;
    /// @brief 指定M10 Registry Objectと同じGeneration／Lifetime境界へ結び付くか返す
    [[nodiscard]] bool is_bound_to(
        const schema::SchemaRegistry &a_schemaRegistry) const noexcept;

  private:
    /// @brief 検証・整列済みValue Schema集合を所有する
    explicit ComponentValueSchemaRegistry(
        std::vector<ComponentValueSchema> a_schemas,
        const schema::SchemaRegistry &a_schemaRegistry) noexcept;

    std::vector<ComponentValueSchema> m_schemas;
    schema::SchemaRegistryGenerationToken m_generationToken;
};

/// @brief Stable FieldIdと型付きValueを所有する既知Field Data
class KnownFieldData final
{
  public:
    /// @brief Stable FieldIdを返す
    [[nodiscard]] schema::FieldId id() const noexcept;
    /// @brief 型付きAuthoring Valueを返す
    [[nodiscard]] const FieldValue &value() const noexcept;

  private:
    friend Result<KnownFieldData> create_known_field(
        schema::FieldId, FieldValue, FieldValueKind,
        const AssertContext &) noexcept;

    /// @brief 検証済みField IdentityとValueを所有する
    KnownFieldData(schema::FieldId a_id, FieldValue a_value) noexcept;

    schema::FieldId m_id;
    FieldValue m_value;
};

/// @brief 未知FieldのStable IdentityとJSON Value TextをLosslessに所有する
class OpaqueFieldData final
{
  public:
    /// @brief 空でないRaw JSON Value Textから未知Fieldを生成する
    [[nodiscard]] static Result<OpaqueFieldData> create(
        schema::FieldId a_id, std::string_view a_rawJson,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Stable FieldIdを返す
    [[nodiscard]] schema::FieldId id() const noexcept;
    /// @brief Serializerが再保存するRaw JSON Value Textを返す
    [[nodiscard]] std::string_view raw_json() const noexcept;

  private:
    /// @brief 検証済み未知Field Payloadを所有する
    OpaqueFieldData(schema::FieldId a_id, std::string a_rawJson) noexcept;

    schema::FieldId m_id;
    std::string m_rawJson;
};

/// @brief 既知Schemaで検証したComponent Authoring Data
class KnownComponentData final
{
  public:
    /// @brief 検証済みComponent Dataを複製する
    KnownComponentData(const KnownComponentData &) = default;
    /// @brief 検証済みComponent Dataを複製代入する
    KnownComponentData &operator=(const KnownComponentData &) = default;
    /// @brief Component Dataを移動し、移動元を無効状態にする
    KnownComponentData(KnownComponentData &&a_other) noexcept;
    /// @brief Component Dataを移動代入し、移動元を無効状態にする
    KnownComponentData &operator=(KnownComponentData &&a_other) noexcept;
    /// @brief Component Data所有値を破棄する
    ~KnownComponentData() = default;

    /// @brief Stable Component Instance Identityを返す
    [[nodiscard]] const ComponentInstanceId &instance_id() const noexcept;
    /// @brief Stable Component Type Identityを返す
    [[nodiscard]] schema::TypeId type_id() const noexcept;
    /// @brief 保存時のSchema Versionを返す
    [[nodiscard]] schema::SchemaVersion schema_version() const noexcept;
    /// @brief Stable FieldId順の既知Field Dataを返す
    [[nodiscard]] std::span<const KnownFieldData> known_fields() const noexcept;
    /// @brief Stable FieldId順の未知Field Dataを返す
    [[nodiscard]] std::span<const OpaqueFieldData> unknown_fields() const noexcept;

  private:
    friend class SceneComponent;
    friend Result<KnownComponentData> create_known_component(
        ComponentInstanceId, schema::TypeId, schema::SchemaVersion,
        std::vector<KnownFieldData>, std::vector<OpaqueFieldData>,
        const schema::SchemaRegistry &,
        const ComponentValueSchemaRegistry &,
        const AssertContext &) noexcept;

    /// @brief 検証済みComponent Identity、Schema、Field Dataを所有する
    KnownComponentData(ComponentInstanceId a_instanceId,
                       schema::TypeId a_typeId,
                       schema::SchemaVersion a_schemaVersion,
                       std::vector<KnownFieldData> a_knownFields,
                       std::vector<OpaqueFieldData> a_unknownFields) noexcept;

    ComponentInstanceId m_instanceId;
    schema::TypeId m_typeId;
    schema::SchemaVersion m_schemaVersion;
    std::vector<KnownFieldData> m_knownFields;
    std::vector<OpaqueFieldData> m_unknownFields;
    bool m_isValid = true;
};

/// @brief 未登録または未来Schema Component Entry全体をLosslessに所有する
class OpaqueComponentData final
{
  public:
    /// @brief 検証済みOpaque Dataを複製する
    OpaqueComponentData(const OpaqueComponentData &) = default;
    /// @brief 検証済みOpaque Dataを複製代入する
    OpaqueComponentData &operator=(const OpaqueComponentData &) = default;
    /// @brief Opaque Dataを移動し、移動元を無効状態にする
    OpaqueComponentData(OpaqueComponentData &&a_other) noexcept;
    /// @brief Opaque Dataを移動代入し、移動元を無効状態にする
    OpaqueComponentData &operator=(OpaqueComponentData &&a_other) noexcept;
    /// @brief Opaque Data所有値を破棄する
    ~OpaqueComponentData() = default;

    /// @brief Identity Metadataを含まない空でないRaw JSON Payloadから未知Componentを生成する
    [[nodiscard]] static Result<OpaqueComponentData> create(
        ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
        schema::SchemaVersion a_schemaVersion, std::string_view a_rawJson,
        const schema::SchemaRegistry &a_schemaRegistry,
        const AssertContext &a_assertContext) noexcept;

    /// @brief Stable Component Instance Identityを返す
    [[nodiscard]] const ComponentInstanceId &instance_id() const noexcept;
    /// @brief Opaque Entry内のStable Type Identityを返す
    [[nodiscard]] schema::TypeId type_id() const noexcept;
    /// @brief Opaque Entry内のSchema Versionを返す
    [[nodiscard]] schema::SchemaVersion schema_version() const noexcept;
    /// @brief Serializerが再出力するOpaque Payloadまたは完全Entry JSON Textを返す
    [[nodiscard]] std::string_view raw_json() const noexcept;
    /// @brief Raw JSONがIdentity Metadataを含む完全Entryならtrueを返す
    [[nodiscard]] bool is_complete_entry() const noexcept;

  private:
    friend class SceneComponent;
    friend class SceneDocumentSerializationAccess;
    /// @brief Serializerが検証済みの完全な未知Component EntryをLossless所有する
    [[nodiscard]] static Result<OpaqueComponentData> create_complete_entry(
        ComponentInstanceId a_instanceId, schema::TypeId a_typeId, schema::SchemaVersion a_schemaVersion,
        std::string_view a_rawJson, const schema::SchemaRegistry &a_schemaRegistry,
        const AssertContext &a_assertContext) noexcept;
    /// @brief 検証済みOpaque Component Entryを所有する
    OpaqueComponentData(ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
                        schema::SchemaVersion a_schemaVersion, std::string a_rawJson,
                        bool a_isCompleteEntry) noexcept;

    ComponentInstanceId m_instanceId;
    schema::TypeId m_typeId;
    schema::SchemaVersion m_schemaVersion;
    std::string m_rawJson;
    bool m_isCompleteEntry = false;
    bool m_isValid = true;
};

/// @brief 既知またはOpaque Component Dataの正確に一方を所有する
class SceneComponent final
{
  public:
    /// @brief 検証済み既知Componentを所有する
    [[nodiscard]] static SceneComponent known(KnownComponentData a_data) noexcept;
    /// @brief Opaque Component Entryを所有する
    [[nodiscard]] static SceneComponent opaque(OpaqueComponentData a_data) noexcept;

    /// @brief Stable Component Instance Identityを返す
    [[nodiscard]] const ComponentInstanceId &instance_id() const noexcept;
    /// @brief Component DataがFactory検証済みの有効状態か返す
    [[nodiscard]] bool is_valid() const noexcept;
    /// @brief 既知Component Dataまたはnullptrを返す
    [[nodiscard]] const KnownComponentData *try_known() const noexcept;
    /// @brief Opaque Component Dataまたはnullptrを返す
    [[nodiscard]] const OpaqueComponentData *try_opaque() const noexcept;

  private:
    using Storage = std::variant<KnownComponentData, OpaqueComponentData>;

    /// @brief 既知またはOpaque Componentの一方を所有する
    explicit SceneComponent(Storage a_storage) noexcept;

    Storage m_storage;
};

/// @brief Field ValueのKind一致を検証して既知Field Dataを生成する
[[nodiscard]] Result<KnownFieldData> create_known_field(
    schema::FieldId a_id, FieldValue a_value, FieldValueKind a_expectedKind,
    const AssertContext &a_assertContext) noexcept;

/// @brief M10 Schema IdentityへField Value Kindを不変結合するSchemaを生成する
[[nodiscard]] Result<ComponentValueSchema> create_component_value_schema(
    schema::TypeId a_typeId, schema::SchemaVersion a_version,
    std::vector<FieldKindBinding> a_fieldKinds,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept;

/// @brief Immutable Value Schema Registryと照合して既知Componentを生成する
/// @details 既知・未知Field合計の上限超過時はResourceLimitExceededを返す
[[nodiscard]] Result<KnownComponentData> create_known_component(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields,
    const schema::SchemaRegistry &a_schemaRegistry,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
    const AssertContext &a_assertContext) noexcept;
} // namespace cue::scene
