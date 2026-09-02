#include <Cue/Scene/ComponentData.h>

#include <Cue/Foundation/Assert.h>
#include <Cue/Scene/Error.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
/// @brief Opaque Field一つを最小Sceneへ埋め込む際にPayload外で必ず必要なJSON Node数
inline constexpr std::size_t k_opaqueFieldEmbeddingNodeReserve = 51U;
/// @brief Opaque Component Payload一つを最小Sceneへ埋め込む際にPayload外で必ず必要なJSON Node数
inline constexpr std::size_t k_opaqueComponentPayloadEmbeddingNodeReserve = 46U;
/// @brief 完全Opaque Component Entry一つを最小Sceneへ埋め込む際にEntry外で必ず必要なJSON Node数
inline constexpr std::size_t k_completeOpaqueComponentEmbeddingNodeReserve = 38U;

/// @brief FieldValueKindが公開契約で定義した列挙値か判定する
[[nodiscard]] bool is_defined_field_value_kind(
    cue::scene::FieldValueKind a_kind) noexcept
{
    using cue::scene::FieldValueKind;
    switch (a_kind)
    {
    case FieldValueKind::Boolean:
    case FieldValueKind::SignedInteger:
    case FieldValueKind::UnsignedInteger:
    case FieldValueKind::FloatingPoint:
    case FieldValueKind::String:
    case FieldValueKind::AssetReference:
        return true;
    }
    return false;
}

/// @brief Scene Component Data Allocation失敗をEmergency終了へ変換する
[[noreturn]] void terminate_scene_allocation(
    const cue::AssertContext &a_assertContext) noexcept
{
    a_assertContext.fatal_handler().terminate(
        "Cue.Scene component data allocation failed");
}

/// @brief UTF-8継続Byteか判定する
[[nodiscard]] bool is_continuation(std::uint8_t a_value) noexcept
{
    return (a_value & 0xC0U) == 0x80U;
}

/// @brief TextがUnicode Scalar列として有効なUTF-8か検証する
[[nodiscard]] bool is_valid_utf8(std::string_view a_value) noexcept
{
    const auto *bytes = reinterpret_cast<const std::uint8_t *>(a_value.data());
    for (std::size_t index = 0U; index < a_value.size();)
    {
        const auto first = bytes[index];
        if (first <= 0x7FU)
        {
            ++index;
            continue;
        }
        if (first >= 0xC2U && first <= 0xDFU)
        {
            if (index + 1U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]))
            {
                return false;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xE0U && first <= 0xEFU)
        {
            if (index + 2U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]) ||
                (first == 0xE0U && bytes[index + 1U] < 0xA0U) ||
                (first == 0xEDU && bytes[index + 1U] >= 0xA0U))
            {
                return false;
            }
            index += 3U;
            continue;
        }
        if (first >= 0xF0U && first <= 0xF4U)
        {
            if (index + 3U >= a_value.size() ||
                !is_continuation(bytes[index + 1U]) ||
                !is_continuation(bytes[index + 2U]) ||
                !is_continuation(bytes[index + 3U]) ||
                (first == 0xF0U && bytes[index + 1U] < 0x90U) ||
                (first == 0xF4U && bytes[index + 1U] >= 0x90U))
            {
                return false;
            }
            index += 4U;
            continue;
        }
        return false;
    }
    return true;
}

/// @brief Opaque PayloadをTree化せず上限付きで検証するFirst-party JSON Reader
class JsonSyntaxValidator final
{
  public:
    /// @brief 検証対象UTF-8 JSON Textを非所有で保持する
    explicit JsonSyntaxValidator(std::string_view a_input,
                                 std::size_t a_reservedNodeCount) noexcept
        : m_input(a_input), m_nodeCount(a_reservedNodeCount)
    {
    }

    /// @brief JSON文書全体と必要なRoot形状を検証する
    [[nodiscard]] bool validate(bool a_requireObject,
                                bool a_rejectIdentityMembers,
                                std::size_t a_embeddingDepth)
    {
        m_rejectIdentityMembers = a_rejectIdentityMembers;
        m_rootObjectDepth = a_embeddingDepth + 1U;
        skip_whitespace();
        if (a_requireObject && peek() != '{')
        {
            return false;
        }
        if (!parse_value(a_embeddingDepth))
        {
            return false;
        }
        skip_whitespace();
        return m_position == m_input.size();
    }

  private:
    /// @brief 次のByteまたは終端のNullを返す
    [[nodiscard]] char peek() const noexcept
    {
        return m_position < m_input.size() ? m_input[m_position] : '\0';
    }

    /// @brief JSON Whitespaceを読み飛ばす
    void skip_whitespace() noexcept
    {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' ||
               peek() == '\n')
        {
            ++m_position;
        }
    }

    /// @brief 現在位置から一つのJSON Valueを検証する
    [[nodiscard]] bool parse_value(std::size_t a_depth)
    {
        if (a_depth > cue::scene::k_maximumSceneNestingDepth)
        {
            return false;
        }
        if (!consume_node())
        {
            return false;
        }
        skip_whitespace();
        switch (peek())
        {
        case '{':
            return parse_object(a_depth + 1U);
        case '[':
            return parse_array(a_depth + 1U);
        case '"':
            return parse_string(nullptr);
        case 't':
            return consume_literal("true");
        case 'f':
            return consume_literal("false");
        case 'n':
            return consume_literal("null");
        default:
            return parse_number();
        }
    }

    /// @brief 重複Memberを拒否してJSON Objectを検証する
    [[nodiscard]] bool parse_object(std::size_t a_depth)
    {
        if (a_depth > cue::scene::k_maximumSceneNestingDepth)
        {
            return false;
        }
        ++m_position;
        skip_whitespace();
        std::vector<std::string> names;
        if (peek() == '}')
        {
            ++m_position;
            return true;
        }
        while (names.size() < cue::scene::k_maximumSceneContainerElements)
        {
            if (!consume_node())
            {
                return false;
            }
            std::string name;
            if (!parse_string(&name) ||
                std::find(names.begin(), names.end(), name) != names.end())
            {
                return false;
            }
            if (a_depth == m_rootObjectDepth && m_rejectIdentityMembers &&
                (name == "componentInstanceId" || name == "typeId" ||
                 name == "schemaVersion"))
            {
                return false;
            }
            names.push_back(std::move(name));
            skip_whitespace();
            if (peek() != ':')
            {
                return false;
            }
            ++m_position;
            if (!parse_value(a_depth))
            {
                return false;
            }
            skip_whitespace();
            if (peek() == '}')
            {
                ++m_position;
                return true;
            }
            if (peek() != ',')
            {
                return false;
            }
            ++m_position;
            skip_whitespace();
        }
        return false;
    }

    /// @brief 要素数上限内のJSON Arrayを検証する
    [[nodiscard]] bool parse_array(std::size_t a_depth)
    {
        if (a_depth > cue::scene::k_maximumSceneNestingDepth)
        {
            return false;
        }
        ++m_position;
        skip_whitespace();
        if (peek() == ']')
        {
            ++m_position;
            return true;
        }
        std::size_t count = 0U;
        while (count < cue::scene::k_maximumSceneContainerElements)
        {
            if (!parse_value(a_depth))
            {
                return false;
            }
            ++count;
            skip_whitespace();
            if (peek() == ']')
            {
                ++m_position;
                return true;
            }
            if (peek() != ',')
            {
                return false;
            }
            ++m_position;
        }
        return false;
    }

    /// @brief Escapeを復号しながらJSON Stringを検証する
    [[nodiscard]] bool parse_string(std::string *a_decoded)
    {
        if (peek() != '"')
        {
            return false;
        }
        ++m_position;
        std::size_t decodedBytes = 0U;
        while (m_position < m_input.size())
        {
            const auto value = static_cast<unsigned char>(m_input[m_position++]);
            if (value == '"')
            {
                return true;
            }
            if (value < 0x20U)
            {
                return false;
            }
            if (value != '\\')
            {
                ++decodedBytes;
                if (decodedBytes > cue::scene::k_maximumSceneStringBytes)
                {
                    return false;
                }
                if (a_decoded != nullptr)
                {
                    a_decoded->push_back(static_cast<char>(value));
                }
                continue;
            }
            if (m_position >= m_input.size())
            {
                return false;
            }
            const char escape = m_input[m_position++];
            if (escape == 'u')
            {
                std::uint32_t codePoint = 0U;
                if (!parse_hex_quad(codePoint))
                {
                    return false;
                }
                if (codePoint >= 0xD800U && codePoint <= 0xDBFFU)
                {
                    if (m_position + 2U > m_input.size() ||
                        m_input[m_position] != '\\' ||
                        m_input[m_position + 1U] != 'u')
                    {
                        return false;
                    }
                    m_position += 2U;
                    std::uint32_t low = 0U;
                    if (!parse_hex_quad(low) || low < 0xDC00U || low > 0xDFFFU)
                    {
                        return false;
                    }
                    codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) +
                                (low - 0xDC00U);
                }
                else if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU)
                {
                    return false;
                }
                const std::size_t encodedBytes =
                    codePoint <= 0x7FU     ? 1U
                    : codePoint <= 0x7FFU  ? 2U
                    : codePoint <= 0xFFFFU ? 3U
                                           : 4U;
                if (decodedBytes >
                    cue::scene::k_maximumSceneStringBytes - encodedBytes)
                {
                    return false;
                }
                decodedBytes += encodedBytes;
                if (a_decoded != nullptr)
                {
                    append_utf8(*a_decoded, codePoint);
                }
                continue;
            }
            char decoded = '\0';
            switch (escape)
            {
            case '"':
                decoded = '"';
                break;
            case '\\':
                decoded = '\\';
                break;
            case '/':
                decoded = '/';
                break;
            case 'b':
                decoded = '\b';
                break;
            case 'f':
                decoded = '\f';
                break;
            case 'n':
                decoded = '\n';
                break;
            case 'r':
                decoded = '\r';
                break;
            case 't':
                decoded = '\t';
                break;
            default:
                return false;
            }
            ++decodedBytes;
            if (decodedBytes > cue::scene::k_maximumSceneStringBytes)
            {
                return false;
            }
            if (a_decoded != nullptr)
            {
                a_decoded->push_back(decoded);
            }
        }
        return false;
    }

    /// @brief 4桁Hexadecimal Unicode Code Unitを読む
    [[nodiscard]] bool parse_hex_quad(std::uint32_t &a_value) noexcept
    {
        if (m_position + 4U > m_input.size())
        {
            return false;
        }
        a_value = 0U;
        for (std::size_t index = 0U; index < 4U; ++index)
        {
            const char value = m_input[m_position++];
            std::uint32_t digit = 0U;
            if (value >= '0' && value <= '9')
            {
                digit = static_cast<std::uint32_t>(value - '0');
            }
            else if (value >= 'a' && value <= 'f')
            {
                digit = static_cast<std::uint32_t>(value - 'a' + 10);
            }
            else if (value >= 'A' && value <= 'F')
            {
                digit = static_cast<std::uint32_t>(value - 'A' + 10);
            }
            else
            {
                return false;
            }
            a_value = (a_value << 4U) | digit;
        }
        return true;
    }

    /// @brief Unicode Scalarをcanonical UTF-8として追記する
    static void append_utf8(std::string &a_output, std::uint32_t a_codePoint)
    {
        if (a_codePoint <= 0x7FU)
        {
            a_output.push_back(static_cast<char>(a_codePoint));
        }
        else if (a_codePoint <= 0x7FFU)
        {
            a_output.push_back(static_cast<char>(0xC0U | (a_codePoint >> 6U)));
            a_output.push_back(static_cast<char>(0x80U | (a_codePoint & 0x3FU)));
        }
        else if (a_codePoint <= 0xFFFFU)
        {
            a_output.push_back(static_cast<char>(0xE0U | (a_codePoint >> 12U)));
            a_output.push_back(static_cast<char>(0x80U | ((a_codePoint >> 6U) & 0x3FU)));
            a_output.push_back(static_cast<char>(0x80U | (a_codePoint & 0x3FU)));
        }
        else
        {
            a_output.push_back(static_cast<char>(0xF0U | (a_codePoint >> 18U)));
            a_output.push_back(static_cast<char>(0x80U | ((a_codePoint >> 12U) & 0x3FU)));
            a_output.push_back(static_cast<char>(0x80U | ((a_codePoint >> 6U) & 0x3FU)));
            a_output.push_back(static_cast<char>(0x80U | (a_codePoint & 0x3FU)));
        }
    }

    /// @brief JSON Number文法を検証する
    [[nodiscard]] bool parse_number() noexcept
    {
        const auto start = m_position;
        if (peek() == '-')
        {
            ++m_position;
        }
        if (peek() == '0')
        {
            ++m_position;
        }
        else if (peek() >= '1' && peek() <= '9')
        {
            while (peek() >= '0' && peek() <= '9')
            {
                ++m_position;
            }
        }
        else
        {
            return false;
        }
        if (peek() == '.')
        {
            ++m_position;
            const auto fractionStart = m_position;
            while (peek() >= '0' && peek() <= '9')
            {
                ++m_position;
            }
            if (fractionStart == m_position)
            {
                return false;
            }
        }
        if (peek() == 'e' || peek() == 'E')
        {
            ++m_position;
            if (peek() == '+' || peek() == '-')
            {
                ++m_position;
            }
            const auto exponentStart = m_position;
            while (peek() >= '0' && peek() <= '9')
            {
                ++m_position;
            }
            if (exponentStart == m_position)
            {
                return false;
            }
        }
        return m_position > start;
    }

    /// @brief 固定JSON Literalを現在位置から消費する
    [[nodiscard]] bool consume_literal(std::string_view a_literal) noexcept
    {
        if (m_input.substr(m_position, a_literal.size()) != a_literal)
        {
            return false;
        }
        m_position += a_literal.size();
        return true;
    }

    /// @brief JSON ValueまたはObject Member一つを文書全体Budgetから消費する
    [[nodiscard]] bool consume_node() noexcept
    {
        if (m_nodeCount >= cue::scene::k_maximumSceneJsonNodes)
        {
            return false;
        }
        ++m_nodeCount;
        return true;
    }

    std::string_view m_input;
    std::size_t m_position = 0U;
    std::size_t m_nodeCount = 0U;
    bool m_rejectIdentityMembers = false;
    std::size_t m_rootObjectDepth = 1U;
};

/// @brief Opaque JSONのUTF-8、構文、重複Member、Root形状を検証する
[[nodiscard]] bool validate_opaque_json(
    std::string_view a_json, bool a_requireObject,
    bool a_rejectIdentityMembers, std::size_t a_embeddingDepth,
    std::size_t a_embeddingNodeReserve,
    const cue::AssertContext &a_assertContext) noexcept
{
    if (a_json.size() > cue::scene::k_maximumSceneBytes ||
        !is_valid_utf8(a_json))
    {
        return false;
    }
    try
    {
        JsonSyntaxValidator validator(a_json, a_embeddingNodeReserve);
        return validator.validate(a_requireObject, a_rejectIdentityMembers,
                                  a_embeddingDepth);
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

/// @brief Field Kind BindingをStable FieldId順で検索する
[[nodiscard]] const cue::scene::FieldKindBinding *find_binding(
    std::span<const cue::scene::FieldKindBinding> a_bindings,
    cue::schema::FieldId a_id) noexcept
{
    const auto found = std::lower_bound(
        a_bindings.begin(), a_bindings.end(), a_id,
        /// @brief BindingのFieldIdを検索値と比較する
        [](const cue::scene::FieldKindBinding &a_binding,
           cue::schema::FieldId a_value) noexcept
        {
            return a_binding.id < a_value;
        });
    return found != a_bindings.end() && found->id == a_id ? &*found : nullptr;
}

/// @brief Schema DescriptorにStable FieldIdが存在するか判定する
[[nodiscard]] bool descriptor_has_field(
    const cue::schema::TypeDescriptor &a_descriptor,
    cue::schema::FieldId a_id) noexcept
{
    return std::any_of(
        a_descriptor.fields().begin(), a_descriptor.fields().end(),
        /// @brief Descriptor FieldのIdentityが検索値と一致するか判定する
        [a_id](const cue::schema::FieldDescriptor &a_field) noexcept
        {
            return a_field.id() == a_id;
        });
}

/// @brief FieldValueのKindと内部Valueが公開不変条件を満たすか判定する
[[nodiscard]] bool is_valid_field_value(
    const cue::scene::FieldValue &a_value) noexcept
{
    using cue::scene::FieldValueKind;
    switch (a_value.kind())
    {
    case FieldValueKind::Boolean:
        return a_value.try_boolean() != nullptr;
    case FieldValueKind::SignedInteger:
        return a_value.try_signed_integer() != nullptr;
    case FieldValueKind::UnsignedInteger:
        return a_value.try_unsigned_integer() != nullptr;
    case FieldValueKind::FloatingPoint:
        return a_value.try_floating_point() != nullptr &&
               std::isfinite(*a_value.try_floating_point());
    case FieldValueKind::String:
        return a_value.try_string() != nullptr &&
               is_valid_utf8(*a_value.try_string());
    case FieldValueKind::AssetReference:
    {
        const auto *reference = a_value.try_asset_reference();
        return reference != nullptr && !reference->token().empty() &&
               is_valid_utf8(reference->token());
    }
    }
    return false;
}
} // namespace

namespace cue::scene
{
AssetReferenceValue::AssetReferenceValue(std::string a_token) noexcept
    : m_token(std::move(a_token))
{
}

Result<AssetReferenceValue> AssetReferenceValue::create(
    std::string_view a_token,
    const AssertContext &a_assertContext) noexcept
{
    if (a_token.empty() || a_token.size() > k_maximumSceneStringBytes ||
        !is_valid_utf8(a_token))
    {
        return Result<AssetReferenceValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Asset reference token must be non-empty valid UTF-8 within 256 KiB"));
    }
    try
    {
        return Result<AssetReferenceValue>::success(
            AssetReferenceValue(std::string(a_token)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

std::string_view AssetReferenceValue::token() const noexcept
{
    return m_token;
}

FieldValue::FieldValue(Storage a_storage) noexcept
    : m_storage(std::move(a_storage))
{
}

FieldValue FieldValue::boolean(bool a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<bool>, a_value));
}

FieldValue FieldValue::signed_integer(std::int64_t a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<std::int64_t>, a_value));
}

FieldValue FieldValue::unsigned_integer(std::uint64_t a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<std::uint64_t>, a_value));
}

Result<FieldValue> FieldValue::floating_point(
    double a_value, const AssertContext &a_assertContext) noexcept
{
    if (!std::isfinite(a_value))
    {
        return Result<FieldValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Scene floating point field must be finite"));
    }
    return Result<FieldValue>::success(
        FieldValue(Storage(std::in_place_type<double>, a_value)));
}

Result<FieldValue> FieldValue::string(
    std::string_view a_value, const AssertContext &a_assertContext) noexcept
{
    if (a_value.size() > k_maximumSceneStringBytes ||
        !is_valid_utf8(a_value))
    {
        return Result<FieldValue>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Scene string field must contain valid UTF-8 within 256 KiB"));
    }
    try
    {
        return Result<FieldValue>::success(FieldValue(
            Storage(std::in_place_type<std::string>, a_value)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

FieldValue FieldValue::asset_reference(AssetReferenceValue a_value) noexcept
{
    return FieldValue(Storage(std::in_place_type<AssetReferenceValue>,
                              std::move(a_value)));
}

FieldValueKind FieldValue::kind() const noexcept
{
    return static_cast<FieldValueKind>(m_storage.index());
}

const bool *FieldValue::try_boolean() const noexcept
{
    return std::get_if<bool>(&m_storage);
}

const std::int64_t *FieldValue::try_signed_integer() const noexcept
{
    return std::get_if<std::int64_t>(&m_storage);
}

const std::uint64_t *FieldValue::try_unsigned_integer() const noexcept
{
    return std::get_if<std::uint64_t>(&m_storage);
}

const double *FieldValue::try_floating_point() const noexcept
{
    return std::get_if<double>(&m_storage);
}

const std::string *FieldValue::try_string() const noexcept
{
    return std::get_if<std::string>(&m_storage);
}

const AssetReferenceValue *FieldValue::try_asset_reference() const noexcept
{
    return std::get_if<AssetReferenceValue>(&m_storage);
}

KnownFieldData::KnownFieldData(schema::FieldId a_id,
                               FieldValue a_value) noexcept
    : m_id(a_id), m_value(std::move(a_value))
{
}

schema::FieldId KnownFieldData::id() const noexcept
{
    return m_id;
}

const FieldValue &KnownFieldData::value() const noexcept
{
    return m_value;
}

OpaqueFieldData::OpaqueFieldData(schema::FieldId a_id,
                                 std::string a_rawJson) noexcept
    : m_id(a_id), m_rawJson(std::move(a_rawJson))
{
}

Result<OpaqueFieldData> OpaqueFieldData::create(
    schema::FieldId a_id, std::string_view a_rawJson,
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty() ||
        !validate_opaque_json(a_rawJson, false, false, 7U,
                              k_opaqueFieldEmbeddingNodeReserve,
                              a_assertContext))
    {
        return Result<OpaqueFieldData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Opaque field JSON payload must not be empty"));
    }
    try
    {
        return Result<OpaqueFieldData>::success(
            OpaqueFieldData(a_id, std::string(a_rawJson)));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

schema::FieldId OpaqueFieldData::id() const noexcept
{
    return m_id;
}

std::string_view OpaqueFieldData::raw_json() const noexcept
{
    return m_rawJson;
}

ComponentValueSchema::ComponentValueSchema(
    schema::TypeId a_typeId, schema::SchemaVersion a_version,
    std::vector<FieldKindBinding> a_fieldKinds,
    const schema::SchemaRegistry &a_schemaRegistry) noexcept
    : m_typeId(a_typeId), m_version(a_version),
      m_fieldKinds(std::move(a_fieldKinds)),
      m_generationToken(a_schemaRegistry.generation_token())
{
}

schema::TypeId ComponentValueSchema::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion ComponentValueSchema::version() const noexcept
{
    return m_version;
}

std::span<const FieldKindBinding> ComponentValueSchema::field_kinds() const noexcept
{
    return m_fieldKinds;
}

ComponentValueSchemaRegistry::ComponentValueSchemaRegistry(
    std::vector<ComponentValueSchema> a_schemas,
    const schema::SchemaRegistry &a_schemaRegistry) noexcept
    : m_schemas(std::move(a_schemas)),
      m_generationToken(a_schemaRegistry.generation_token())
{
}

Result<ComponentValueSchemaRegistry> ComponentValueSchemaRegistry::create(
    std::vector<ComponentValueSchema> a_schemas,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    try
    {
        std::sort(a_schemas.begin(), a_schemas.end(),
                  /// @brief Value SchemaをStable TypeId順へ並べる
                  [](const ComponentValueSchema &a_left,
                     const ComponentValueSchema &a_right) noexcept
                  {
                      return a_left.type_id() < a_right.type_id();
                  });
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
    for (std::size_t index = 1U; index < a_schemas.size(); ++index)
    {
        if (a_schemas[index - 1U].type_id() == a_schemas[index].type_id())
        {
            return Result<ComponentValueSchemaRegistry>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schema registry contains a duplicate TypeId"));
        }
    }
    for (const auto &schema : a_schemas)
    {
        if (!(schema.m_generationToken == a_schemaRegistry.generation_token()))
        {
            return Result<ComponentValueSchemaRegistry>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schemas must share one M10 registry generation"));
        }
        auto descriptorResult = a_schemaRegistry.find(schema.type_id(),
                                                       a_assertContext);
        if (!descriptorResult ||
            (*descriptorResult.try_value())->version() != schema.version() ||
            (*descriptorResult.try_value())->fields().size() !=
                schema.field_kinds().size())
        {
            return Result<ComponentValueSchemaRegistry>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schema no longer matches its M10 descriptor"));
        }
        for (const auto &binding : schema.field_kinds())
        {
            if (!is_defined_field_value_kind(binding.kind) ||
                !descriptor_has_field(**descriptorResult.try_value(), binding.id))
            {
                return Result<ComponentValueSchemaRegistry>::failure(
                    make_scene_error(
                        a_assertContext, SceneError::InvalidComponentData,
                        "Component value schema contains an invalid field binding"));
            }
        }
    }
    return Result<ComponentValueSchemaRegistry>::success(
        ComponentValueSchemaRegistry(std::move(a_schemas), a_schemaRegistry));
}

bool ComponentValueSchemaRegistry::is_bound_to(
    const schema::SchemaRegistry &a_schemaRegistry) const noexcept
{
    return m_generationToken == a_schemaRegistry.generation_token();
}

const ComponentValueSchema *ComponentValueSchemaRegistry::find(
    schema::TypeId a_typeId) const noexcept
{
    const auto found = std::lower_bound(
        m_schemas.begin(), m_schemas.end(), a_typeId,
        /// @brief Value SchemaのTypeIdを検索値と比較する
        [](const ComponentValueSchema &a_schema,
           schema::TypeId a_value) noexcept
        {
            return a_schema.type_id() < a_value;
        });
    return found != m_schemas.end() && found->type_id() == a_typeId
               ? &*found
               : nullptr;
}

KnownComponentData::KnownComponentData(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields) noexcept
    : m_instanceId(std::move(a_instanceId)), m_typeId(a_typeId),
      m_schemaVersion(a_schemaVersion),
      m_knownFields(std::move(a_knownFields)),
      m_unknownFields(std::move(a_unknownFields))
{
}

KnownComponentData::KnownComponentData(KnownComponentData &&a_other) noexcept
    : m_instanceId(std::move(a_other.m_instanceId)),
      m_typeId(a_other.m_typeId), m_schemaVersion(a_other.m_schemaVersion),
      m_knownFields(std::move(a_other.m_knownFields)),
      m_unknownFields(std::move(a_other.m_unknownFields)),
      m_isValid(std::exchange(a_other.m_isValid, false))
{
}

KnownComponentData &KnownComponentData::operator=(
    KnownComponentData &&a_other) noexcept
{
    if (this != &a_other)
    {
        m_instanceId = std::move(a_other.m_instanceId);
        m_typeId = a_other.m_typeId;
        m_schemaVersion = a_other.m_schemaVersion;
        m_knownFields = std::move(a_other.m_knownFields);
        m_unknownFields = std::move(a_other.m_unknownFields);
        m_isValid = std::exchange(a_other.m_isValid, false);
    }
    return *this;
}

const ComponentInstanceId &KnownComponentData::instance_id() const noexcept
{
    return m_instanceId;
}

schema::TypeId KnownComponentData::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion KnownComponentData::schema_version() const noexcept
{
    return m_schemaVersion;
}

std::span<const KnownFieldData> KnownComponentData::known_fields() const noexcept
{
    return m_knownFields;
}

std::span<const OpaqueFieldData> KnownComponentData::unknown_fields() const noexcept
{
    return m_unknownFields;
}

OpaqueComponentData::OpaqueComponentData(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion, std::string a_rawJson,
    bool a_isCompleteEntry) noexcept
    : m_instanceId(std::move(a_instanceId)), m_typeId(a_typeId),
      m_schemaVersion(a_schemaVersion), m_rawJson(std::move(a_rawJson)),
      m_isCompleteEntry(a_isCompleteEntry)
{
}

OpaqueComponentData::OpaqueComponentData(
    OpaqueComponentData &&a_other) noexcept
    : m_instanceId(std::move(a_other.m_instanceId)),
      m_typeId(a_other.m_typeId), m_schemaVersion(a_other.m_schemaVersion),
      m_rawJson(std::move(a_other.m_rawJson)),
      m_isCompleteEntry(std::exchange(a_other.m_isCompleteEntry, false)),
      m_isValid(std::exchange(a_other.m_isValid, false))
{
}

OpaqueComponentData &OpaqueComponentData::operator=(
    OpaqueComponentData &&a_other) noexcept
{
    if (this != &a_other)
    {
        m_instanceId = std::move(a_other.m_instanceId);
        m_typeId = a_other.m_typeId;
        m_schemaVersion = a_other.m_schemaVersion;
        m_rawJson = std::move(a_other.m_rawJson);
        m_isCompleteEntry = std::exchange(a_other.m_isCompleteEntry, false);
        m_isValid = std::exchange(a_other.m_isValid, false);
    }
    return *this;
}

Result<OpaqueComponentData> OpaqueComponentData::create(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion, std::string_view a_rawJson,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty() ||
        !validate_opaque_json(
            a_rawJson, true, true, 5U,
            k_opaqueComponentPayloadEmbeddingNodeReserve, a_assertContext))
    {
        return Result<OpaqueComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Opaque component JSON entry must not be empty"));
    }
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (descriptorResult)
    {
        const auto *descriptor = *descriptorResult.try_value();
        if (a_schemaVersion <= descriptor->version())
        {
            return Result<OpaqueComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidOpaqueData,
                "Registered component type is opaque only for a future schema version"));
        }
    }
    try
    {
        return Result<OpaqueComponentData>::success(OpaqueComponentData(
            std::move(a_instanceId), a_typeId, a_schemaVersion,
            std::string(a_rawJson), false));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

Result<OpaqueComponentData> OpaqueComponentData::create_complete_entry(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion, std::string_view a_rawJson,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    if (a_rawJson.empty() ||
        !validate_opaque_json(
            a_rawJson, true, false, 4U,
            k_completeOpaqueComponentEmbeddingNodeReserve, a_assertContext))
    {
        return Result<OpaqueComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Complete opaque component entry must be a valid JSON object"));
    }
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (descriptorResult &&
        a_schemaVersion <= (*descriptorResult.try_value())->version())
    {
        return Result<OpaqueComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidOpaqueData,
            "Registered component type is opaque only for a future schema version"));
    }
    try
    {
        return Result<OpaqueComponentData>::success(OpaqueComponentData(
            std::move(a_instanceId), a_typeId, a_schemaVersion,
            std::string(a_rawJson), true));
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
}

const ComponentInstanceId &OpaqueComponentData::instance_id() const noexcept
{
    return m_instanceId;
}

schema::TypeId OpaqueComponentData::type_id() const noexcept
{
    return m_typeId;
}

schema::SchemaVersion OpaqueComponentData::schema_version() const noexcept
{
    return m_schemaVersion;
}

std::string_view OpaqueComponentData::raw_json() const noexcept
{
    return m_rawJson;
}

bool OpaqueComponentData::is_complete_entry() const noexcept
{
    return m_isCompleteEntry;
}

SceneComponent::SceneComponent(Storage a_storage) noexcept
    : m_storage(std::move(a_storage))
{
}

SceneComponent SceneComponent::known(KnownComponentData a_data) noexcept
{
    return SceneComponent(Storage(std::in_place_type<KnownComponentData>,
                                  std::move(a_data)));
}

SceneComponent SceneComponent::opaque(OpaqueComponentData a_data) noexcept
{
    return SceneComponent(Storage(std::in_place_type<OpaqueComponentData>,
                                  std::move(a_data)));
}

const ComponentInstanceId &SceneComponent::instance_id() const noexcept
{
    if (const auto *knownData = try_known(); knownData != nullptr)
    {
        return knownData->instance_id();
    }
    return std::get<OpaqueComponentData>(m_storage).instance_id();
}

bool SceneComponent::is_valid() const noexcept
{
    if (const auto *knownData = try_known(); knownData != nullptr)
    {
        return knownData->m_isValid;
    }
    return std::get<OpaqueComponentData>(m_storage).m_isValid;
}

const KnownComponentData *SceneComponent::try_known() const noexcept
{
    return std::get_if<KnownComponentData>(&m_storage);
}

const OpaqueComponentData *SceneComponent::try_opaque() const noexcept
{
    return std::get_if<OpaqueComponentData>(&m_storage);
}

Result<KnownFieldData> create_known_field(
    schema::FieldId a_id, FieldValue a_value,
    FieldValueKind a_expectedKind,
    const AssertContext &a_assertContext) noexcept
{
    if (!is_valid_field_value(a_value) || a_value.kind() != a_expectedKind)
    {
        return Result<KnownFieldData>::failure(make_scene_error(
            a_assertContext, SceneError::FieldTypeMismatch,
            "Scene field value kind does not match its declared binding"));
    }
    return Result<KnownFieldData>::success(
        KnownFieldData(a_id, std::move(a_value)));
}

Result<ComponentValueSchema> create_component_value_schema(
    schema::TypeId a_typeId, schema::SchemaVersion a_version,
    std::vector<FieldKindBinding> a_fieldKinds,
    const schema::SchemaRegistry &a_schemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    auto descriptorResult = a_schemaRegistry.find(a_typeId, a_assertContext);
    if (!descriptorResult)
    {
        return Result<ComponentValueSchema>::failure(make_scene_error(
            a_assertContext, SceneError::UnknownSchemaType,
            "Component value schema requires a registered TypeId"));
    }
    const auto *descriptor = *descriptorResult.try_value();
    if (descriptor->version() != a_version ||
        descriptor->fields().size() != a_fieldKinds.size())
    {
        return Result<ComponentValueSchema>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Component value schema version or field set does not match M10 schema"));
    }
    try
    {
        std::sort(a_fieldKinds.begin(), a_fieldKinds.end(),
                  /// @brief Field Kind BindingをStable FieldId順へ並べる
                  [](const FieldKindBinding &a_left,
                     const FieldKindBinding &a_right) noexcept
                  {
                      return a_left.id < a_right.id;
                  });
    }
    catch (...)
    {
        terminate_scene_allocation(a_assertContext);
    }
    for (std::size_t index = 0U; index < a_fieldKinds.size(); ++index)
    {
        if (!is_defined_field_value_kind(a_fieldKinds[index].kind) ||
            !descriptor_has_field(*descriptor, a_fieldKinds[index].id) ||
            (index > 0U &&
             !(a_fieldKinds[index - 1U].id < a_fieldKinds[index].id)))
        {
            return Result<ComponentValueSchema>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidComponentData,
                "Component value schema fields must match M10 schema exactly"));
        }
    }
    return Result<ComponentValueSchema>::success(ComponentValueSchema(
        a_typeId, a_version, std::move(a_fieldKinds), a_schemaRegistry));
}

Result<KnownComponentData> create_known_component(
    ComponentInstanceId a_instanceId, schema::TypeId a_typeId,
    schema::SchemaVersion a_schemaVersion,
    std::vector<KnownFieldData> a_knownFields,
    std::vector<OpaqueFieldData> a_unknownFields,
    const schema::SchemaRegistry &a_schemaRegistry,
    const ComponentValueSchemaRegistry &a_valueSchemaRegistry,
    const AssertContext &a_assertContext) noexcept
{
    if (a_knownFields.size() > k_maximumSceneContainerElements ||
        a_unknownFields.size() >
            k_maximumSceneContainerElements - a_knownFields.size())
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Scene component field count exceeds the 4096 element limit"));
    }
    if (!a_valueSchemaRegistry.is_bound_to(a_schemaRegistry))
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Component value schema belongs to a different M10 registry generation"));
    }
    const auto *valueSchema = a_valueSchemaRegistry.find(a_typeId);
    if (valueSchema == nullptr)
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::UnknownSchemaType,
            "Known scene component requires a registered schema type"));
    }
    if (valueSchema->version() != a_schemaVersion)
    {
        return Result<KnownComponentData>::failure(make_scene_error(
            a_assertContext, SceneError::InvalidComponentData,
            "Known scene component schema version or field binding set is invalid"));
    }

    const auto fieldKinds = valueSchema->field_kinds();

    std::sort(a_knownFields.begin(), a_knownFields.end(),
              /// @brief Known FieldをStable FieldId順へ並べる
              [](const KnownFieldData &a_left,
                 const KnownFieldData &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });
    std::sort(a_unknownFields.begin(), a_unknownFields.end(),
              /// @brief Unknown FieldをStable FieldId順へ並べる
              [](const OpaqueFieldData &a_left,
                 const OpaqueFieldData &a_right) noexcept
              {
                  return a_left.id() < a_right.id();
              });

    for (std::size_t index = 0U; index < a_knownFields.size(); ++index)
    {
        const auto id = a_knownFields[index].id();
        if (index > 0U && a_knownFields[index - 1U].id() == id)
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::DuplicateFieldId,
                "Known scene component contains a duplicate FieldId"));
        }
        const auto *binding = find_binding(fieldKinds, id);
        if (!is_valid_field_value(a_knownFields[index].value()) ||
            binding == nullptr ||
            binding->kind != a_knownFields[index].value().kind())
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext,
                binding == nullptr ? SceneError::UnknownSchemaField
                                   : SceneError::FieldTypeMismatch,
                "Known scene field does not match its schema binding"));
        }
    }

    for (std::size_t index = 0U; index < a_unknownFields.size(); ++index)
    {
        const auto id = a_unknownFields[index].id();
        if (a_unknownFields[index].raw_json().empty() ||
            !validate_opaque_json(
                a_unknownFields[index].raw_json(), false, false, 7U,
                k_opaqueFieldEmbeddingNodeReserve, a_assertContext))
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::InvalidOpaqueData,
                "Opaque scene field payload is no longer valid JSON"));
        }
        if (find_binding(fieldKinds, id) != nullptr ||
            (index > 0U && a_unknownFields[index - 1U].id() == id) ||
            std::any_of(a_knownFields.begin(), a_knownFields.end(),
                        /// @brief Known FieldとUnknown FieldのIdentity衝突を判定する
                        [id](const KnownFieldData &a_field) noexcept
                        {
                            return a_field.id() == id;
                        }))
        {
            return Result<KnownComponentData>::failure(make_scene_error(
                a_assertContext, SceneError::DuplicateFieldId,
                "Opaque scene field identity conflicts with known or opaque data"));
        }
    }

    return Result<KnownComponentData>::success(KnownComponentData(
        std::move(a_instanceId), a_typeId, a_schemaVersion,
        std::move(a_knownFields), std::move(a_unknownFields)));
}
} // namespace cue::scene
