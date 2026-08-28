#pragma once

#include <Cue/Math/Quaternion.h>

namespace cue::math
{
/// @brief Translation、単位Quaternion Rotation、Scaleを所有するTRS値型
class Transform final
{
  public:
    /// @brief Identity Transformを生成する
    Transform() noexcept = default;

    /// @brief 有限なTranslationとScale、および単位QuaternionからTransformを生成する
    [[nodiscard]] static cue::Result<Transform> create(
        cue::EmergencyHandler &a_emergencyHandler, Vector3 a_translation,
        Quaternion a_rotation, Vector3 a_scale, const Tolerance &a_tolerance) noexcept;

    /// @brief Translationを値で返す
    [[nodiscard]] Vector3 translation() const noexcept;
    /// @brief Rotationを値で返す
    [[nodiscard]] Quaternion rotation() const noexcept;
    /// @brief Scaleを値で返す
    [[nodiscard]] Vector3 scale() const noexcept;

  private:
    /// @brief 検証済みTRS値を保持する
    Transform(Vector3 a_translation, Quaternion a_rotation, Vector3 a_scale) noexcept;

    Vector3 m_translation{};
    Quaternion m_rotation{};
    Vector3 m_scale{1.0F, 1.0F, 1.0F};
};

/// @brief TransformをScale、Rotation、Translation順のMatrix4へ変換する
[[nodiscard]] cue::Result<Matrix4> to_matrix4(cue::EmergencyHandler &a_emergencyHandler,
                                               const Transform &a_transform,
                                               const Tolerance &a_tolerance) noexcept;
/// @brief TransformでPointをScale、Rotation、Translation順に変換する
[[nodiscard]] cue::Result<Vector3> transform_point(cue::EmergencyHandler &a_emergencyHandler,
                                                    Vector3 a_value,
                                                    const Transform &a_transform,
                                                    const Tolerance &a_tolerance) noexcept;
/// @brief TransformでDirectionをScale、Rotation順に変換する
[[nodiscard]] cue::Result<Vector3> transform_direction(
    cue::EmergencyHandler &a_emergencyHandler, Vector3 a_value, const Transform &a_transform,
    const Tolerance &a_tolerance) noexcept;
/// @brief first、secondの時間順にTransformを合成し、Shearを保持できるMatrix4を返す
[[nodiscard]] cue::Result<Matrix4> compose(cue::EmergencyHandler &a_emergencyHandler,
                                            const Transform &a_first,
                                            const Transform &a_second,
                                            const Tolerance &a_tolerance) noexcept;
/// @brief 可逆なTransformの逆変換Matrixを返す
[[nodiscard]] cue::Result<Matrix4> inverse_matrix(cue::EmergencyHandler &a_emergencyHandler,
                                                   const Transform &a_transform,
                                                   const Tolerance &a_tolerance) noexcept;
/// @brief Shearを持たない有限かつ非特異なAffine Matrix4をTransformへ分解する
[[nodiscard]] cue::Result<Transform> decompose(cue::EmergencyHandler &a_emergencyHandler,
                                                const Matrix4 &a_matrix,
                                                const Tolerance &a_tolerance) noexcept;
} // namespace cue::math
