#include <Cue/Math/Quaternion.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace cue::math
{
namespace
{
/// @brief Math DomainのQuaternion Errorを生成する
[[nodiscard]] cue::Error make_quaternion_error(cue::EmergencyHandler &a_emergencyHandler,
                                               std::int64_t a_code,
                                               std::string_view a_summary) noexcept
{
    auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", a_code);
    return cue::Error::create(a_emergencyHandler, std::move(errorCode), a_summary);
}

/// @brief Hamilton ProductをOperand順どおりに計算する
[[nodiscard]] Quaternion hamilton_product(Quaternion a_left, Quaternion a_right) noexcept
{
    return {
        a_left.w * a_right.x + a_left.x * a_right.w + a_left.y * a_right.z -
            a_left.z * a_right.y,
        a_left.w * a_right.y - a_left.x * a_right.z + a_left.y * a_right.w +
            a_left.z * a_right.x,
        a_left.w * a_right.z + a_left.x * a_right.y - a_left.y * a_right.x +
            a_left.z * a_right.w,
        a_left.w * a_right.w - a_left.x * a_right.x - a_left.y * a_right.y -
            a_left.z * a_right.z,
    };
}
} // namespace

/// @brief Quaternionを組込みfloatの完全一致規則で比較する
bool operator==(Quaternion a_left, Quaternion a_right) noexcept
{
    return a_left.x == a_right.x && a_left.y == a_right.y && a_left.z == a_right.z &&
           a_left.w == a_right.w;
}

/// @brief Quaternionが完全一致しない場合にtrueを返す
bool operator!=(Quaternion a_left, Quaternion a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Quaternionの全Componentが有限な場合にtrueを返す
bool is_finite(Quaternion a_value) noexcept
{
    return is_finite(a_value.x) && is_finite(a_value.y) && is_finite(a_value.z) &&
           is_finite(a_value.w);
}

/// @brief Quaternionの長さをOverflowとUnderflowを避けて計算する
float length(Quaternion a_value) noexcept
{
    return length(Vector4{a_value.x, a_value.y, a_value.z, a_value.w});
}

/// @brief Quaternionの全Componentを指定Tolerance内で比較する
bool is_near(Quaternion a_left, Quaternion a_right, const Tolerance &a_tolerance) noexcept
{
    return is_near(a_left.x, a_right.x, a_tolerance) &&
           is_near(a_left.y, a_right.y, a_tolerance) &&
           is_near(a_left.z, a_right.z, a_tolerance) &&
           is_near(a_left.w, a_right.w, a_tolerance);
}

/// @brief 符号が反転したQuaternionも同じ回転として指定Tolerance内で比較する
bool is_same_rotation(Quaternion a_left, Quaternion a_right,
                      const Tolerance &a_tolerance) noexcept
{
    const auto negated = Quaternion{-a_right.x, -a_right.y, -a_right.z, -a_right.w};
    return is_near(a_left, a_right, a_tolerance) || is_near(a_left, negated, a_tolerance);
}

/// @brief Quaternionが有限かつ指定Tolerance内で単位長の場合にtrueを返す
bool is_unit_rotation(Quaternion a_value, const Tolerance &a_tolerance) noexcept
{
    return is_finite(a_value) && is_near(length(a_value), 1.0F, a_tolerance);
}

/// @brief 有限で十分な長さを持つQuaternionを正規化する
cue::Result<Quaternion> normalize(cue::EmergencyHandler &a_emergencyHandler,
                                  Quaternion a_value,
                                  const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 1,
                                           "Quaternion must be finite");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    const auto maximum = std::max(
        {std::abs(a_value.x), std::abs(a_value.y), std::abs(a_value.z), std::abs(a_value.w)});

    if (maximum <= a_tolerance.absolute() && length(a_value) <= a_tolerance.absolute())
    {
        auto error = make_quaternion_error(a_emergencyHandler, 2,
                                           "Quaternion is too small to normalize");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    const auto scaled = Quaternion{a_value.x / maximum, a_value.y / maximum,
                                   a_value.z / maximum, a_value.w / maximum};
    const auto scaledLength = length(scaled);
    auto normalized = Quaternion{scaled.x / scaledLength, scaled.y / scaledLength,
                                 scaled.z / scaledLength, scaled.w / scaledLength};

    if (!is_unit_rotation(normalized, a_tolerance))
    {
        auto error = make_quaternion_error(
            a_emergencyHandler, 2,
            "Normalized Quaternion is not unit length within the requested tolerance");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    return cue::Result<Quaternion>::success(std::move(normalized));
}

/// @brief 有限で十分な長さを持つQuaternionの逆Quaternionを返す
cue::Result<Quaternion> inverse(cue::EmergencyHandler &a_emergencyHandler,
                                Quaternion a_value,
                                const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 1,
                                           "Quaternion must be finite");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    const auto maximum = std::max(
        {std::abs(a_value.x), std::abs(a_value.y), std::abs(a_value.z), std::abs(a_value.w)});

    if (maximum <= a_tolerance.absolute() && length(a_value) <= a_tolerance.absolute())
    {
        auto error = make_quaternion_error(a_emergencyHandler, 2,
                                           "Quaternion is too small to invert");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    const auto scaled = Quaternion{a_value.x / maximum, a_value.y / maximum,
                                   a_value.z / maximum, a_value.w / maximum};
    const auto scaledNormSquared = static_cast<double>(scaled.x) * scaled.x +
                                   static_cast<double>(scaled.y) * scaled.y +
                                   static_cast<double>(scaled.z) * scaled.z +
                                   static_cast<double>(scaled.w) * scaled.w;
    const auto inverseScale = 1.0 / (static_cast<double>(maximum) * scaledNormSquared);
    auto result = Quaternion{static_cast<float>(-scaled.x * inverseScale),
                             static_cast<float>(-scaled.y * inverseScale),
                             static_cast<float>(-scaled.z * inverseScale),
                             static_cast<float>(scaled.w * inverseScale)};

    if (!is_finite(result))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 2,
                                           "Quaternion inverse is not representable as float");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    return cue::Result<Quaternion>::success(std::move(result));
}

/// @brief 有限で十分な長さを持つAxisとRadian角から単位Quaternionを生成する
cue::Result<Quaternion> from_axis_angle(cue::EmergencyHandler &a_emergencyHandler,
                                        Vector3 a_axis, Radians a_angle,
                                        const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_angle.value))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 1,
                                           "Rotation angle must be finite");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    auto axisResult = normalize(a_emergencyHandler, a_axis, a_tolerance);

    if (!axisResult)
    {
        auto error = std::move(*axisResult.try_error());
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    const auto axis = *axisResult.try_value();
    const auto halfAngle = a_angle.value * 0.5F;
    const auto sine = std::sin(halfAngle);
    auto rotation = Quaternion{axis.x * sine, axis.y * sine, axis.z * sine,
                               std::cos(halfAngle)};
    return normalize(a_emergencyHandler, rotation, a_tolerance);
}

/// @brief 単位Quaternionを行Vector規約の回転Matrix3へ変換する
cue::Result<Matrix3> to_matrix3(cue::EmergencyHandler &a_emergencyHandler,
                                Quaternion a_rotation,
                                const Tolerance &a_tolerance) noexcept
{
    if (!is_unit_rotation(a_rotation, a_tolerance))
    {
        auto error = make_quaternion_error(a_emergencyHandler,
                                           is_finite(a_rotation) ? 2 : 1,
                                           "Rotation Quaternion must be finite and unit length");
        return cue::Result<Matrix3>::failure(std::move(error));
    }

    const auto xx = a_rotation.x * a_rotation.x;
    const auto yy = a_rotation.y * a_rotation.y;
    const auto zz = a_rotation.z * a_rotation.z;
    const auto xy = a_rotation.x * a_rotation.y;
    const auto xz = a_rotation.x * a_rotation.z;
    const auto yz = a_rotation.y * a_rotation.z;
    const auto xw = a_rotation.x * a_rotation.w;
    const auto yw = a_rotation.y * a_rotation.w;
    const auto zw = a_rotation.z * a_rotation.w;
    auto result = Matrix3{};
    result.values[0][0] = 1.0F - 2.0F * (yy + zz);
    result.values[0][1] = 2.0F * (xy + zw);
    result.values[0][2] = 2.0F * (xz - yw);
    result.values[1][0] = 2.0F * (xy - zw);
    result.values[1][1] = 1.0F - 2.0F * (xx + zz);
    result.values[1][2] = 2.0F * (yz + xw);
    result.values[2][0] = 2.0F * (xz + yw);
    result.values[2][1] = 2.0F * (yz - xw);
    result.values[2][2] = 1.0F - 2.0F * (xx + yy);
    return cue::Result<Matrix3>::success(std::move(result));
}

/// @brief 単位Quaternionを行Vector規約の回転Matrix4へ変換する
cue::Result<Matrix4> to_matrix4(cue::EmergencyHandler &a_emergencyHandler,
                                Quaternion a_rotation,
                                const Tolerance &a_tolerance) noexcept
{
    auto matrix3Result = to_matrix3(a_emergencyHandler, a_rotation, a_tolerance);

    if (!matrix3Result)
    {
        auto error = std::move(*matrix3Result.try_error());
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    const auto &matrix3 = *matrix3Result.try_value();
    auto result = Matrix4{};

    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            result.values[row][column] = matrix3.values[row][column];
        }
    }

    return cue::Result<Matrix4>::success(std::move(result));
}

/// @brief 単位QuaternionでVector3を回転する
cue::Result<Vector3> rotate(cue::EmergencyHandler &a_emergencyHandler, Vector3 a_value,
                            Quaternion a_rotation,
                            const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 1,
                                           "Rotated Vector3 must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto matrixResult = to_matrix3(a_emergencyHandler, a_rotation, a_tolerance);

    if (!matrixResult)
    {
        auto error = std::move(*matrixResult.try_error());
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto result = a_value * *matrixResult.try_value();

    if (!is_finite(result))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 1,
                                           "Quaternion rotation result must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    return cue::Result<Vector3>::success(std::move(result));
}

/// @brief first、secondの時間順に単位Quaternion回転を合成する
cue::Result<Quaternion> compose_rotation(cue::EmergencyHandler &a_emergencyHandler,
                                         Quaternion a_first, Quaternion a_second,
                                         const Tolerance &a_tolerance) noexcept
{
    if (!is_unit_rotation(a_first, a_tolerance) ||
        !is_unit_rotation(a_second, a_tolerance))
    {
        const auto finite = is_finite(a_first) && is_finite(a_second);
        auto error = make_quaternion_error(a_emergencyHandler, finite ? 2 : 1,
                                           "Composed Quaternion values must be finite and unit length");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    auto result = hamilton_product(a_second, a_first);

    if (!is_unit_rotation(result, a_tolerance))
    {
        auto error = make_quaternion_error(a_emergencyHandler, 2,
                                           "Quaternion composition exceeded the requested tolerance");
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    return cue::Result<Quaternion>::success(std::move(result));
}
} // namespace cue::math
