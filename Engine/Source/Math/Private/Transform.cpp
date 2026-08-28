#include <Cue/Math/Transform.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

namespace cue::math
{
namespace
{
/// @brief Math DomainのTransform Errorを生成する
[[nodiscard]] cue::Error make_transform_error(cue::EmergencyHandler &a_emergencyHandler,
                                              std::int64_t a_code,
                                              std::string_view a_summary) noexcept
{
    auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", a_code);
    return cue::Error::create(a_emergencyHandler, std::move(errorCode), a_summary);
}

/// @brief Proper Rotationを表す行Vector Matrix3からQuaternionを生成する
[[nodiscard]] cue::Result<Quaternion> quaternion_from_matrix(
    cue::EmergencyHandler &a_emergencyHandler, const Matrix3 &a_matrix,
    const Tolerance &a_tolerance) noexcept
{
    const auto c00 = static_cast<double>(a_matrix.values[0][0]);
    const auto c01 = static_cast<double>(a_matrix.values[1][0]);
    const auto c02 = static_cast<double>(a_matrix.values[2][0]);
    const auto c10 = static_cast<double>(a_matrix.values[0][1]);
    const auto c11 = static_cast<double>(a_matrix.values[1][1]);
    const auto c12 = static_cast<double>(a_matrix.values[2][1]);
    const auto c20 = static_cast<double>(a_matrix.values[0][2]);
    const auto c21 = static_cast<double>(a_matrix.values[1][2]);
    const auto c22 = static_cast<double>(a_matrix.values[2][2]);
    const auto trace = c00 + c11 + c22;
    auto result = Quaternion{};

    if (trace > 0.0)
    {
        const auto scale = std::sqrt(trace + 1.0) * 2.0;
        result.w = static_cast<float>(0.25 * scale);
        result.x = static_cast<float>((c21 - c12) / scale);
        result.y = static_cast<float>((c02 - c20) / scale);
        result.z = static_cast<float>((c10 - c01) / scale);
    }
    else if (c00 > c11 && c00 > c22)
    {
        const auto scale = std::sqrt(1.0 + c00 - c11 - c22) * 2.0;
        result.w = static_cast<float>((c21 - c12) / scale);
        result.x = static_cast<float>(0.25 * scale);
        result.y = static_cast<float>((c01 + c10) / scale);
        result.z = static_cast<float>((c02 + c20) / scale);
    }
    else if (c11 > c22)
    {
        const auto scale = std::sqrt(1.0 + c11 - c00 - c22) * 2.0;
        result.w = static_cast<float>((c02 - c20) / scale);
        result.x = static_cast<float>((c01 + c10) / scale);
        result.y = static_cast<float>(0.25 * scale);
        result.z = static_cast<float>((c12 + c21) / scale);
    }
    else
    {
        const auto scale = std::sqrt(1.0 + c22 - c00 - c11) * 2.0;
        result.w = static_cast<float>((c10 - c01) / scale);
        result.x = static_cast<float>((c02 + c20) / scale);
        result.y = static_cast<float>((c12 + c21) / scale);
        result.z = static_cast<float>(0.25 * scale);
    }

    auto normalizedResult = normalize(a_emergencyHandler, result, a_tolerance);

    if (!normalizedResult)
    {
        auto error = std::move(*normalizedResult.try_error());
        return cue::Result<Quaternion>::failure(std::move(error));
    }

    auto normalized = *normalizedResult.try_value();

    if (normalized.w < 0.0F)
    {
        normalized = {-normalized.x, -normalized.y, -normalized.z, -normalized.w};
    }

    return cue::Result<Quaternion>::success(std::move(normalized));
}
} // namespace

/// @brief 検証済みTRS値を保持する
Transform::Transform(Vector3 a_translation, Quaternion a_rotation, Vector3 a_scale) noexcept
    : m_translation(a_translation), m_rotation(a_rotation), m_scale(a_scale)
{
}

/// @brief 有限なTranslationとScale、および単位QuaternionからTransformを生成する
cue::Result<Transform> Transform::create(cue::EmergencyHandler &a_emergencyHandler,
                                         Vector3 a_translation, Quaternion a_rotation,
                                         Vector3 a_scale,
                                         const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_translation) || !is_finite(a_scale) || !is_finite(a_rotation))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transform values must be finite");
        return cue::Result<Transform>::failure(std::move(error));
    }

    if (!is_unit_rotation(a_rotation, a_tolerance))
    {
        auto error = make_transform_error(a_emergencyHandler, 2,
                                          "Transform rotation must be unit length");
        return cue::Result<Transform>::failure(std::move(error));
    }

    auto result = Transform(a_translation, a_rotation, a_scale);
    return cue::Result<Transform>::success(std::move(result));
}

/// @brief Translationを値で返す
Vector3 Transform::translation() const noexcept
{
    return m_translation;
}

/// @brief Rotationを値で返す
Quaternion Transform::rotation() const noexcept
{
    return m_rotation;
}

/// @brief Scaleを値で返す
Vector3 Transform::scale() const noexcept
{
    return m_scale;
}

/// @brief TransformをScale、Rotation、Translation順のMatrix4へ変換する
cue::Result<Matrix4> to_matrix4(cue::EmergencyHandler &a_emergencyHandler,
                                const Transform &a_transform,
                                const Tolerance &a_tolerance) noexcept
{
    auto rotationResult = to_matrix4(a_emergencyHandler, a_transform.rotation(), a_tolerance);

    if (!rotationResult)
    {
        auto error = std::move(*rotationResult.try_error());
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    auto result = *rotationResult.try_value();
    const auto scale = a_transform.scale();

    for (std::size_t column = 0; column < 3; ++column)
    {
        result.values[0][column] *= scale.x;
        result.values[1][column] *= scale.y;
        result.values[2][column] *= scale.z;
    }

    const auto translation = a_transform.translation();
    result.values[3][0] = translation.x;
    result.values[3][1] = translation.y;
    result.values[3][2] = translation.z;
    return cue::Result<Matrix4>::success(std::move(result));
}

/// @brief TransformでPointをScale、Rotation、Translation順に変換する
cue::Result<Vector3> transform_point(cue::EmergencyHandler &a_emergencyHandler,
                                     Vector3 a_value, const Transform &a_transform,
                                     const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transformed point must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto matrixResult = to_matrix4(a_emergencyHandler, a_transform, a_tolerance);

    if (!matrixResult)
    {
        auto error = std::move(*matrixResult.try_error());
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto result = cue::math::transform_point(a_value, *matrixResult.try_value());

    if (!is_finite(result))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transform point result must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    return cue::Result<Vector3>::success(std::move(result));
}

/// @brief TransformでDirectionをScale、Rotation順に変換する
cue::Result<Vector3> transform_direction(cue::EmergencyHandler &a_emergencyHandler,
                                         Vector3 a_value, const Transform &a_transform,
                                         const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_value))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transformed direction must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto matrixResult = to_matrix4(a_emergencyHandler, a_transform, a_tolerance);

    if (!matrixResult)
    {
        auto error = std::move(*matrixResult.try_error());
        return cue::Result<Vector3>::failure(std::move(error));
    }

    auto result = cue::math::transform_direction(a_value, *matrixResult.try_value());

    if (!is_finite(result))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transform direction result must be finite");
        return cue::Result<Vector3>::failure(std::move(error));
    }

    return cue::Result<Vector3>::success(std::move(result));
}

/// @brief first、secondの時間順にTransformを合成し、Shearを保持できるMatrix4を返す
cue::Result<Matrix4> compose(cue::EmergencyHandler &a_emergencyHandler,
                             const Transform &a_first, const Transform &a_second,
                             const Tolerance &a_tolerance) noexcept
{
    auto firstResult = to_matrix4(a_emergencyHandler, a_first, a_tolerance);

    if (!firstResult)
    {
        auto error = std::move(*firstResult.try_error());
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    auto secondResult = to_matrix4(a_emergencyHandler, a_second, a_tolerance);

    if (!secondResult)
    {
        auto error = std::move(*secondResult.try_error());
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    auto result = *firstResult.try_value() * *secondResult.try_value();

    if (!is_finite(result))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Transform composition result must be finite");
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    return cue::Result<Matrix4>::success(std::move(result));
}

/// @brief 可逆なTransformの逆変換Matrixを返す
cue::Result<Matrix4> inverse_matrix(cue::EmergencyHandler &a_emergencyHandler,
                                    const Transform &a_transform,
                                    const Tolerance &a_tolerance) noexcept
{
    const auto scale = a_transform.scale();

    if (std::abs(scale.x) <= a_tolerance.absolute() ||
        std::abs(scale.y) <= a_tolerance.absolute() ||
        std::abs(scale.z) <= a_tolerance.absolute())
    {
        auto error = make_transform_error(a_emergencyHandler, 3,
                                          "Transform with degenerate scale is not invertible");
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    auto matrixResult = to_matrix4(a_emergencyHandler, a_transform, a_tolerance);

    if (!matrixResult)
    {
        auto error = std::move(*matrixResult.try_error());
        return cue::Result<Matrix4>::failure(std::move(error));
    }

    return inverse(a_emergencyHandler, *matrixResult.try_value(), a_tolerance);
}

/// @brief Shearを持たない有限かつ非特異なAffine Matrix4をTransformへ分解する
cue::Result<Transform> decompose(cue::EmergencyHandler &a_emergencyHandler,
                                 const Matrix4 &a_matrix,
                                 const Tolerance &a_tolerance) noexcept
{
    if (!is_finite(a_matrix))
    {
        auto error = make_transform_error(a_emergencyHandler, 1,
                                          "Decomposed Matrix must be finite");
        return cue::Result<Transform>::failure(std::move(error));
    }

    if (!is_near(a_matrix.values[0][3], 0.0F, a_tolerance) ||
        !is_near(a_matrix.values[1][3], 0.0F, a_tolerance) ||
        !is_near(a_matrix.values[2][3], 0.0F, a_tolerance) ||
        !is_near(a_matrix.values[3][3], 1.0F, a_tolerance))
    {
        auto error = make_transform_error(a_emergencyHandler, 4,
                                          "Matrix is not an affine TRS transform");
        return cue::Result<Transform>::failure(std::move(error));
    }

    auto row0 = Vector3{a_matrix.values[0][0], a_matrix.values[0][1],
                        a_matrix.values[0][2]};
    auto row1 = Vector3{a_matrix.values[1][0], a_matrix.values[1][1],
                        a_matrix.values[1][2]};
    auto row2 = Vector3{a_matrix.values[2][0], a_matrix.values[2][1],
                        a_matrix.values[2][2]};
    auto scale = Vector3{length(row0), length(row1), length(row2)};

    if (scale.x <= a_tolerance.absolute() || scale.y <= a_tolerance.absolute() ||
        scale.z <= a_tolerance.absolute())
    {
        auto error = make_transform_error(a_emergencyHandler, 4,
                                          "Matrix has a degenerate scale axis");
        return cue::Result<Transform>::failure(std::move(error));
    }

    row0 = row0 / scale.x;
    row1 = row1 / scale.y;
    row2 = row2 / scale.z;

    if (!is_near(dot(row0, row1), 0.0F, a_tolerance) ||
        !is_near(dot(row0, row2), 0.0F, a_tolerance) ||
        !is_near(dot(row1, row2), 0.0F, a_tolerance))
    {
        auto error = make_transform_error(a_emergencyHandler, 4,
                                          "Matrix contains shear and cannot be represented as TRS");
        return cue::Result<Transform>::failure(std::move(error));
    }

    const auto determinant = dot(row0, cross(row1, row2));

    if (is_near(determinant, 0.0F, a_tolerance))
    {
        auto error = make_transform_error(a_emergencyHandler, 4,
                                          "Matrix basis is not decomposable");
        return cue::Result<Transform>::failure(std::move(error));
    }

    if (determinant < 0.0F)
    {
        scale.x = -scale.x;
        row0 = -row0;
    }

    auto rotationMatrix = Matrix3{};
    rotationMatrix.values[0][0] = row0.x;
    rotationMatrix.values[0][1] = row0.y;
    rotationMatrix.values[0][2] = row0.z;
    rotationMatrix.values[1][0] = row1.x;
    rotationMatrix.values[1][1] = row1.y;
    rotationMatrix.values[1][2] = row1.z;
    rotationMatrix.values[2][0] = row2.x;
    rotationMatrix.values[2][1] = row2.y;
    rotationMatrix.values[2][2] = row2.z;
    auto rotationResult = quaternion_from_matrix(a_emergencyHandler, rotationMatrix, a_tolerance);

    if (!rotationResult)
    {
        auto error = std::move(*rotationResult.try_error());
        return cue::Result<Transform>::failure(std::move(error));
    }

    const auto translation = Vector3{a_matrix.values[3][0], a_matrix.values[3][1],
                                     a_matrix.values[3][2]};
    auto transformResult = Transform::create(a_emergencyHandler, translation,
                                             *rotationResult.try_value(), scale, a_tolerance);

    if (!transformResult)
    {
        auto error = std::move(*transformResult.try_error());
        return cue::Result<Transform>::failure(std::move(error));
    }

    auto reconstructedResult = to_matrix4(a_emergencyHandler, *transformResult.try_value(),
                                          a_tolerance);

    if (!reconstructedResult ||
        !is_near(*reconstructedResult.try_value(), a_matrix, a_tolerance))
    {
        auto error = make_transform_error(a_emergencyHandler, 4,
                                          "Matrix reconstruction exceeded the requested tolerance");
        return cue::Result<Transform>::failure(std::move(error));
    }

    return transformResult;
}
} // namespace cue::math
