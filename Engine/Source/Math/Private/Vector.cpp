#include <Cue/Math/Vector.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace cue::math
{
namespace
{
/// @brief Component列の長さを最大絶対ComponentでScaleして安全に計算する
[[nodiscard]] float scaled_length(std::span<const float> a_components) noexcept
{
    auto maximum = 0.0F;

    for (const auto component : a_components)
    {
        if (std::isnan(component))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }

        if (std::isinf(component))
        {
            return std::numeric_limits<float>::infinity();
        }

        maximum = std::max(maximum, std::abs(component));
    }

    if (maximum == 0.0F)
    {
        return 0.0F;
    }

    auto scaledSquareSum = 0.0F;

    for (const auto component : a_components)
    {
        const auto scaled = component / maximum;
        scaledSquareSum += scaled * scaled;
    }

    return maximum * std::sqrt(scaledSquareSum);
}

/// @brief Math Domainの分類済みErrorを生成する
[[nodiscard]] cue::Error make_math_error(cue::EmergencyHandler &a_emergencyHandler,
                                         std::int64_t a_code,
                                         std::string_view a_summary) noexcept
{
    auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", a_code);
    return cue::Error::create(a_emergencyHandler, std::move(errorCode), a_summary);
}
} // namespace

/// @brief Vector2の対応成分を加算する
Vector2 operator+(Vector2 a_left, Vector2 a_right) noexcept
{
    return {a_left.x + a_right.x, a_left.y + a_right.y};
}

/// @brief Vector2の対応成分を減算する
Vector2 operator-(Vector2 a_left, Vector2 a_right) noexcept
{
    return {a_left.x - a_right.x, a_left.y - a_right.y};
}

/// @brief Vector2の全成分の符号を反転する
Vector2 operator-(Vector2 a_value) noexcept
{
    return {-a_value.x, -a_value.y};
}

/// @brief Vector2の全成分へScalarを乗算する
Vector2 operator*(Vector2 a_value, float a_scalar) noexcept
{
    return {a_value.x * a_scalar, a_value.y * a_scalar};
}

/// @brief ScalarをVector2の全成分へ乗算する
Vector2 operator*(float a_scalar, Vector2 a_value) noexcept
{
    return a_value * a_scalar;
}

/// @brief Vector2の全成分をScalarで除算する
Vector2 operator/(Vector2 a_value, float a_scalar) noexcept
{
    return {a_value.x / a_scalar, a_value.y / a_scalar};
}

/// @brief Vector2を組込みfloatの完全一致規則で比較する
bool operator==(Vector2 a_left, Vector2 a_right) noexcept
{
    return a_left.x == a_right.x && a_left.y == a_right.y;
}

/// @brief Vector2が完全一致しない場合にtrueを返す
bool operator!=(Vector2 a_left, Vector2 a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Vector3の対応成分を加算する
Vector3 operator+(Vector3 a_left, Vector3 a_right) noexcept
{
    return {a_left.x + a_right.x, a_left.y + a_right.y, a_left.z + a_right.z};
}

/// @brief Vector3の対応成分を減算する
Vector3 operator-(Vector3 a_left, Vector3 a_right) noexcept
{
    return {a_left.x - a_right.x, a_left.y - a_right.y, a_left.z - a_right.z};
}

/// @brief Vector3の全成分の符号を反転する
Vector3 operator-(Vector3 a_value) noexcept
{
    return {-a_value.x, -a_value.y, -a_value.z};
}

/// @brief Vector3の全成分へScalarを乗算する
Vector3 operator*(Vector3 a_value, float a_scalar) noexcept
{
    return {a_value.x * a_scalar, a_value.y * a_scalar, a_value.z * a_scalar};
}

/// @brief ScalarをVector3の全成分へ乗算する
Vector3 operator*(float a_scalar, Vector3 a_value) noexcept
{
    return a_value * a_scalar;
}

/// @brief Vector3の全成分をScalarで除算する
Vector3 operator/(Vector3 a_value, float a_scalar) noexcept
{
    return {a_value.x / a_scalar, a_value.y / a_scalar, a_value.z / a_scalar};
}

/// @brief Vector3を組込みfloatの完全一致規則で比較する
bool operator==(Vector3 a_left, Vector3 a_right) noexcept
{
    return a_left.x == a_right.x && a_left.y == a_right.y && a_left.z == a_right.z;
}

/// @brief Vector3が完全一致しない場合にtrueを返す
bool operator!=(Vector3 a_left, Vector3 a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Vector4の対応成分を加算する
Vector4 operator+(Vector4 a_left, Vector4 a_right) noexcept
{
    return {a_left.x + a_right.x, a_left.y + a_right.y, a_left.z + a_right.z,
            a_left.w + a_right.w};
}

/// @brief Vector4の対応成分を減算する
Vector4 operator-(Vector4 a_left, Vector4 a_right) noexcept
{
    return {a_left.x - a_right.x, a_left.y - a_right.y, a_left.z - a_right.z,
            a_left.w - a_right.w};
}

/// @brief Vector4の全成分の符号を反転する
Vector4 operator-(Vector4 a_value) noexcept
{
    return {-a_value.x, -a_value.y, -a_value.z, -a_value.w};
}

/// @brief Vector4の全成分へScalarを乗算する
Vector4 operator*(Vector4 a_value, float a_scalar) noexcept
{
    return {a_value.x * a_scalar, a_value.y * a_scalar, a_value.z * a_scalar,
            a_value.w * a_scalar};
}

/// @brief ScalarをVector4の全成分へ乗算する
Vector4 operator*(float a_scalar, Vector4 a_value) noexcept
{
    return a_value * a_scalar;
}

/// @brief Vector4の全成分をScalarで除算する
Vector4 operator/(Vector4 a_value, float a_scalar) noexcept
{
    return {a_value.x / a_scalar, a_value.y / a_scalar, a_value.z / a_scalar,
            a_value.w / a_scalar};
}

/// @brief Vector4を組込みfloatの完全一致規則で比較する
bool operator==(Vector4 a_left, Vector4 a_right) noexcept
{
    return a_left.x == a_right.x && a_left.y == a_right.y && a_left.z == a_right.z &&
           a_left.w == a_right.w;
}

/// @brief Vector4が完全一致しない場合にtrueを返す
bool operator!=(Vector4 a_left, Vector4 a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Vector2の内積を返す
float dot(Vector2 a_left, Vector2 a_right) noexcept
{
    return a_left.x * a_right.x + a_left.y * a_right.y;
}

/// @brief Vector3の内積を返す
float dot(Vector3 a_left, Vector3 a_right) noexcept
{
    return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z;
}

/// @brief Vector4の内積を返す
float dot(Vector4 a_left, Vector4 a_right) noexcept
{
    return a_left.x * a_right.x + a_left.y * a_right.y + a_left.z * a_right.z +
           a_left.w * a_right.w;
}

/// @brief 右手則の代数定義に従うVector3の外積を返す
Vector3 cross(Vector3 a_left, Vector3 a_right) noexcept
{
    return {a_left.y * a_right.z - a_left.z * a_right.y,
            a_left.z * a_right.x - a_left.x * a_right.z,
            a_left.x * a_right.y - a_left.y * a_right.x};
}

/// @brief Vector2の長さをOverflowとUnderflowを避けて計算する
float length(Vector2 a_value) noexcept
{
    const float components[] = {a_value.x, a_value.y};
    return scaled_length(components);
}

/// @brief Vector3の長さをOverflowとUnderflowを避けて計算する
float length(Vector3 a_value) noexcept
{
    const float components[] = {a_value.x, a_value.y, a_value.z};
    return scaled_length(components);
}

/// @brief Vector4の長さをOverflowとUnderflowを避けて計算する
float length(Vector4 a_value) noexcept
{
    const float components[] = {a_value.x, a_value.y, a_value.z, a_value.w};
    return scaled_length(components);
}

/// @brief 有限なVector2の全成分を指定Tolerance内で比較する
bool is_near(Vector2 a_left, Vector2 a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.x, a_right.x, a_tolerance) &&
           is_near(a_left.y, a_right.y, a_tolerance);
}

/// @brief 有限なVector3の全成分を指定Tolerance内で比較する
bool is_near(Vector3 a_left, Vector3 a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.x, a_right.x, a_tolerance) &&
           is_near(a_left.y, a_right.y, a_tolerance) &&
           is_near(a_left.z, a_right.z, a_tolerance);
}

/// @brief 有限なVector4の全成分を指定Tolerance内で比較する
bool is_near(Vector4 a_left, Vector4 a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.x, a_right.x, a_tolerance) &&
           is_near(a_left.y, a_right.y, a_tolerance) &&
           is_near(a_left.z, a_right.z, a_tolerance) &&
           is_near(a_left.w, a_right.w, a_tolerance);
}

/// @brief Vector2の全成分が有限な場合にtrueを返す
bool is_finite(Vector2 a_value) noexcept
{
    return is_finite(a_value.x) && is_finite(a_value.y);
}

/// @brief Vector3の全成分が有限な場合にtrueを返す
bool is_finite(Vector3 a_value) noexcept
{
    return is_finite(a_value.x) && is_finite(a_value.y) && is_finite(a_value.z);
}

/// @brief Vector4の全成分が有限な場合にtrueを返す
bool is_finite(Vector4 a_value) noexcept
{
    return is_finite(a_value.x) && is_finite(a_value.y) && is_finite(a_value.z) &&
           is_finite(a_value.w);
}

/// @brief 有限で十分な長さを持つVector2を正規化する
cue::Result<Vector2> normalize(cue::EmergencyHandler &a_emergencyHandler, Vector2 a_value,
                               const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_math_error(a_emergencyHandler, 1, "Vector2 must be finite");
        return cue::Result<Vector2>::failure(std::move(error));
    }

    const auto maximum = std::max(std::abs(a_value.x), std::abs(a_value.y));

    if (maximum <= a_tolerance.absolute() && length(a_value) <= a_tolerance.absolute())
    {
        auto error = make_math_error(a_emergencyHandler, 2,
                                     "Vector2 is too small to normalize");
        return cue::Result<Vector2>::failure(std::move(error));
    }

    const auto scaled = a_value / maximum;
    auto normalized = scaled / length(scaled);
    return cue::Result<Vector2>::success(std::move(normalized));
}

/// @brief 有限で十分な長さを持つVector3を正規化する
cue::Result<Vector3> normalize(cue::EmergencyHandler &a_emergencyHandler, Vector3 a_value,
                               const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_math_error(a_emergencyHandler, 1, "Vector3 must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    const auto maximum =
        std::max({std::abs(a_value.x), std::abs(a_value.y), std::abs(a_value.z)});

    if (maximum <= a_tolerance.absolute() && length(a_value) <= a_tolerance.absolute())
    {
        auto error = make_math_error(a_emergencyHandler, 2,
                                     "Vector3 is too small to normalize");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    const auto scaled = a_value / maximum;
    auto normalized = scaled / length(scaled);
    return cue::Result<Vector3>::success(std::move(normalized));
}

/// @brief 有限で十分な長さを持つVector4を正規化する
cue::Result<Vector4> normalize(cue::EmergencyHandler &a_emergencyHandler, Vector4 a_value,
                               const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_math_error(a_emergencyHandler, 1, "Vector4 must be finite");
        return cue::Result<Vector4>::failure(std::move(error));
    }

    const auto maximum = std::max(
        {std::abs(a_value.x), std::abs(a_value.y), std::abs(a_value.z), std::abs(a_value.w)});

    if (maximum <= a_tolerance.absolute() && length(a_value) <= a_tolerance.absolute())
    {
        auto error = make_math_error(a_emergencyHandler, 2,
                                     "Vector4 is too small to normalize");
        return cue::Result<Vector4>::failure(std::move(error));
    }

    const auto scaled = a_value / maximum;
    auto normalized = scaled / length(scaled);
    return cue::Result<Vector4>::success(std::move(normalized));
}
} // namespace cue::math
