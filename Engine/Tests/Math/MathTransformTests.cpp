#include <Cue/Math/Transform.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace
{
class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
    /// @brief Test中に予期しないEmergency終了経路へ到達した場合はProcessを異常終了する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief Resultが指定したCue.Math Error Codeを保持することを検証する
template <typename T>
[[nodiscard]] bool has_math_error(cue::Result<T> &a_result, std::int64_t a_code)
{
    const auto *error = a_result.try_error();
    return error != nullptr && error->code().domain() == "Cue.Math" &&
           error->code().value() == a_code;
}

/// @brief 固定値Testで共有するToleranceを生成する
[[nodiscard]] bool create_test_tolerance(TestEmergencyHandler &a_handler,
                                         cue::math::Tolerance &a_tolerance)
{
    auto result = cue::math::Tolerance::create(a_handler, 0.0001F, 0.0001F);

    if (!result)
    {
        return false;
    }

    a_tolerance = *result.try_value();
    return true;
}

/// @brief Matrix3とMatrix4の既定値がIdentityで、時間順の行列積になることを検証する
[[nodiscard]] bool test_matrix_identity_and_order(const cue::math::Tolerance &a_tolerance)
{
    auto scale = cue::math::Matrix4{};
    scale.values[0][0] = 2.0F;
    scale.values[1][1] = 3.0F;
    scale.values[2][2] = 4.0F;
    auto translation = cue::math::Matrix4{};
    translation.values[3][0] = 10.0F;
    translation.values[3][1] = 20.0F;
    translation.values[3][2] = 30.0F;
    const auto composed = scale * translation;
    const auto point = cue::math::transform_point(cue::math::Vector3{1.0F, 1.0F, 1.0F},
                                                  composed);
    const auto direction = cue::math::transform_direction(
        cue::math::Vector3{1.0F, 1.0F, 1.0F}, composed);

    return cue::math::Matrix3{} == cue::math::Matrix3{} &&
           cue::math::Matrix4{} == cue::math::Matrix4{} &&
           cue::math::is_near(point, cue::math::Vector3{12.0F, 23.0F, 34.0F},
                              a_tolerance) &&
           cue::math::is_near(direction, cue::math::Vector3{2.0F, 3.0F, 4.0F},
                              a_tolerance);
}

/// @brief Matrix3とMatrix4のInverseが両側のIdentityを復元することを検証する
[[nodiscard]] bool test_matrix_inverse(TestEmergencyHandler &a_handler,
                                       const cue::math::Tolerance &a_tolerance)
{
    auto matrix3 = cue::math::Matrix3{};
    matrix3.values[0][0] = 2.0F;
    matrix3.values[1][1] = -4.0F;
    matrix3.values[2][2] = 0.5F;
    auto matrix4 = cue::math::Matrix4{};
    matrix4.values[0][0] = 2.0F;
    matrix4.values[1][1] = 3.0F;
    matrix4.values[2][2] = 4.0F;
    matrix4.values[3][0] = 5.0F;
    matrix4.values[3][1] = 6.0F;
    matrix4.values[3][2] = 7.0F;
    auto inverse3 = cue::math::inverse(a_handler, matrix3, a_tolerance);
    auto inverse4 = cue::math::inverse(a_handler, matrix4, a_tolerance);

    return inverse3 && inverse4 &&
           cue::math::is_near(matrix3 * *inverse3.try_value(), cue::math::Matrix3{},
                              a_tolerance) &&
           cue::math::is_near(*inverse3.try_value() * matrix3, cue::math::Matrix3{},
                              a_tolerance) &&
           cue::math::is_near(matrix4 * *inverse4.try_value(), cue::math::Matrix4{},
                              a_tolerance) &&
           cue::math::is_near(*inverse4.try_value() * matrix4, cue::math::Matrix4{},
                              a_tolerance);
}

/// @brief 特異Matrixと非有限MatrixのInverseが分類済みErrorを返すことを検証する
[[nodiscard]] bool test_matrix_inverse_failures(TestEmergencyHandler &a_handler,
                                                const cue::math::Tolerance &a_tolerance)
{
    auto singular3 = cue::math::inverse(a_handler, cue::math::zero_matrix3(), a_tolerance);
    auto singular4 = cue::math::inverse(a_handler, cue::math::zero_matrix4(), a_tolerance);
    auto nonFiniteMatrix = cue::math::Matrix4{};
    nonFiniteMatrix.values[1][2] = std::numeric_limits<float>::quiet_NaN();
    auto nonFinite = cue::math::inverse(a_handler, nonFiniteMatrix, a_tolerance);

    return has_math_error(singular3, 3) && has_math_error(singular4, 3) &&
           has_math_error(nonFinite, 1);
}

/// @brief Matrix特異判定が一様Scaleに依存せずTolerance境界を守ることを検証する
[[nodiscard]] bool test_matrix_inverse_scale_awareness(TestEmergencyHandler &a_handler)
{
    auto toleranceResult = cue::math::Tolerance::create(a_handler, 0.0F, 0.001F);

    if (!toleranceResult)
    {
        return false;
    }

    const auto tolerance = *toleranceResult.try_value();
    auto wellConditioned = cue::math::Matrix3{};
    wellConditioned.values[0][1] = 0.25F;
    auto largeWellConditioned = cue::math::zero_matrix3();
    auto smallWellConditioned = cue::math::zero_matrix3();
    auto nearSingular = cue::math::Matrix3{};
    nearSingular.values[0][1] = 1.0F;
    nearSingular.values[1][0] = 1.0F;
    nearSingular.values[1][1] = 1.0005F;
    auto largeNearSingular = cue::math::zero_matrix3();
    auto smallNearSingular = cue::math::zero_matrix3();

    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            largeWellConditioned.values[row][column] =
                wellConditioned.values[row][column] * 1000000.0F;
            smallWellConditioned.values[row][column] =
                wellConditioned.values[row][column] * 0.000001F;
            largeNearSingular.values[row][column] =
                nearSingular.values[row][column] * 1000000.0F;
            smallNearSingular.values[row][column] =
                nearSingular.values[row][column] * 0.000001F;
        }
    }

    auto regular = cue::math::inverse(a_handler, wellConditioned, tolerance);
    auto regularLarge = cue::math::inverse(a_handler, largeWellConditioned, tolerance);
    auto regularSmall = cue::math::inverse(a_handler, smallWellConditioned, tolerance);
    auto singular = cue::math::inverse(a_handler, nearSingular, tolerance);
    auto singularLarge = cue::math::inverse(a_handler, largeNearSingular, tolerance);
    auto singularSmall = cue::math::inverse(a_handler, smallNearSingular, tolerance);

    return regular && regularLarge && regularSmall && has_math_error(singular, 3) &&
           has_math_error(singularLarge, 3) && has_math_error(singularSmall, 3);
}

/// @brief float化したInverseが指定Toleranceで両側Identityを復元できない場合を拒否する
[[nodiscard]] bool test_matrix_inverse_precision_failure(TestEmergencyHandler &a_handler)
{
    auto toleranceResult = cue::math::Tolerance::create(a_handler, 0.0001F, 0.0001F);

    if (!toleranceResult)
    {
        return false;
    }

    auto matrix = cue::math::Matrix3{};
    matrix.values[0][0] = -3.0E-15F;
    matrix.values[0][1] = -0.003F;
    matrix.values[0][2] = 0.0F;
    matrix.values[1][0] = -3.0F;
    matrix.values[1][1] = -1000.0F;
    matrix.values[1][2] = 0.0F;
    auto result = cue::math::inverse(a_handler, matrix, *toleranceResult.try_value());
    return has_math_error(result, 3);
}

/// @brief ADR-0011のX、Y、Z正回転BasisをQuaternion固定値で検証する
[[nodiscard]] bool test_quaternion_basis(TestEmergencyHandler &a_handler,
                                         const cue::math::Tolerance &a_tolerance)
{
    const auto quarterTurn = cue::math::Radians(cue::math::pi() * 0.5F);
    auto xRotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F}, quarterTurn, a_tolerance);
    auto yRotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 1.0F, 0.0F}, quarterTurn, a_tolerance);
    auto zRotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 0.0F, 1.0F}, quarterTurn, a_tolerance);

    if (!xRotation || !yRotation || !zRotation)
    {
        return false;
    }

    auto rotatedX = cue::math::rotate(a_handler, cue::math::Vector3{0.0F, 1.0F, 0.0F},
                                      *xRotation.try_value(), a_tolerance);
    auto rotatedY = cue::math::rotate(a_handler, cue::math::Vector3{0.0F, 0.0F, 1.0F},
                                      *yRotation.try_value(), a_tolerance);
    auto rotatedZ = cue::math::rotate(a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F},
                                      *zRotation.try_value(), a_tolerance);

    return rotatedX && rotatedY && rotatedZ &&
           cue::math::is_near(*rotatedX.try_value(),
                              cue::math::Vector3{0.0F, 0.0F, 1.0F}, a_tolerance) &&
           cue::math::is_near(*rotatedY.try_value(),
                              cue::math::Vector3{1.0F, 0.0F, 0.0F}, a_tolerance) &&
           cue::math::is_near(*rotatedZ.try_value(),
                              cue::math::Vector3{0.0F, 1.0F, 0.0F}, a_tolerance);
}

/// @brief Quaternion時間順合成とMatrix時間順合成が同じ回転になることを検証する
[[nodiscard]] bool test_quaternion_composition(TestEmergencyHandler &a_handler,
                                               const cue::math::Tolerance &a_tolerance)
{
    const auto quarterTurn = cue::math::Radians(cue::math::pi() * 0.5F);
    auto first = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F}, quarterTurn, a_tolerance);
    auto second = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 1.0F, 0.0F}, quarterTurn, a_tolerance);

    if (!first || !second)
    {
        return false;
    }

    auto composed = cue::math::compose_rotation(a_handler, *first.try_value(),
                                                *second.try_value(), a_tolerance);
    auto firstMatrix = cue::math::to_matrix3(a_handler, *first.try_value(), a_tolerance);
    auto secondMatrix = cue::math::to_matrix3(a_handler, *second.try_value(), a_tolerance);

    if (!composed || !firstMatrix || !secondMatrix)
    {
        return false;
    }

    auto composedMatrix = cue::math::to_matrix3(a_handler, *composed.try_value(), a_tolerance);
    return composedMatrix &&
           cue::math::is_near(*composedMatrix.try_value(),
                              *firstMatrix.try_value() * *secondMatrix.try_value(), a_tolerance);
}

/// @brief 非単位・Zero・非有限Quaternionの失敗契約と符号反転同一回転を検証する
[[nodiscard]] bool test_quaternion_failures(TestEmergencyHandler &a_handler,
                                            const cue::math::Tolerance &a_tolerance)
{
    auto strictToleranceResult = cue::math::Tolerance::create(a_handler, 0.0F, 0.0F);

    if (!strictToleranceResult)
    {
        return false;
    }

    const auto strictTolerance = *strictToleranceResult.try_value();
    auto nonUnit = cue::math::to_matrix3(a_handler, cue::math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F},
                                         a_tolerance);
    auto zero = cue::math::normalize(a_handler,
                                     cue::math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F},
                                     a_tolerance);
    auto nonFinite = cue::math::inverse(
        a_handler,
        cue::math::Quaternion{std::numeric_limits<float>::infinity(), 0.0F, 0.0F, 1.0F},
        a_tolerance);
    const auto identity = cue::math::Quaternion{};
    const auto negatedIdentity = cue::math::Quaternion{-0.0F, -0.0F, -0.0F, -1.0F};
    const auto maximum = std::numeric_limits<float>::max();
    auto extremeInverse = cue::math::inverse(
        a_handler, cue::math::Quaternion{maximum, maximum, 0.0F, 0.0F}, a_tolerance);
    auto strictNormalized = cue::math::normalize(
        a_handler, cue::math::Quaternion{1.0F, 1.0F, 1.0F, 0.0F}, strictTolerance);
    auto strictAxisRotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{1.0F, 1.0F, 1.0F}, cue::math::Radians(1.0F),
        strictTolerance);
    const auto strictAxisRotationUsable =
        !strictAxisRotation ||
        cue::math::is_unit_rotation(*strictAxisRotation.try_value(), strictTolerance);

    return has_math_error(nonUnit, 2) && has_math_error(zero, 2) &&
           has_math_error(nonFinite, 1) &&
           cue::math::is_same_rotation(identity, negatedIdentity, a_tolerance) &&
           extremeInverse && extremeInverse.try_value()->x < 0.0F &&
           extremeInverse.try_value()->y < 0.0F && has_math_error(strictNormalized, 2) &&
           strictAxisRotationUsable;
}

/// @brief TransformがScale、Rotation、Translation順にPointとDirectionを変換することを検証する
[[nodiscard]] bool test_transform_order(TestEmergencyHandler &a_handler,
                                        const cue::math::Tolerance &a_tolerance)
{
    auto rotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 0.0F, 1.0F},
        cue::math::Radians(cue::math::pi() * 0.5F), a_tolerance);

    if (!rotation)
    {
        return false;
    }

    auto transform = cue::math::Transform::create(
        a_handler, cue::math::Vector3{10.0F, 20.0F, 30.0F}, *rotation.try_value(),
        cue::math::Vector3{2.0F, 3.0F, 4.0F}, a_tolerance);

    if (!transform)
    {
        return false;
    }

    auto point = cue::math::transform_point(a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F},
                                            *transform.try_value(), a_tolerance);
    auto direction = cue::math::transform_direction(
        a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F}, *transform.try_value(),
        a_tolerance);

    return point && direction &&
           cue::math::is_near(*point.try_value(), cue::math::Vector3{10.0F, 22.0F, 30.0F},
                              a_tolerance) &&
           cue::math::is_near(*direction.try_value(), cue::math::Vector3{0.0F, 2.0F, 0.0F},
                              a_tolerance);
}

/// @brief Transform合成と逆Matrixが時間順変換とPoint往復を保つことを検証する
[[nodiscard]] bool test_transform_composition_and_inverse(
    TestEmergencyHandler &a_handler, const cue::math::Tolerance &a_tolerance)
{
    auto first = cue::math::Transform::create(
        a_handler, cue::math::Vector3{1.0F, 0.0F, 0.0F}, cue::math::Quaternion{},
        cue::math::Vector3{2.0F, 2.0F, 2.0F}, a_tolerance);
    auto second = cue::math::Transform::create(
        a_handler, cue::math::Vector3{0.0F, 3.0F, 0.0F}, cue::math::Quaternion{},
        cue::math::Vector3{1.0F, 1.0F, 1.0F}, a_tolerance);

    if (!first || !second)
    {
        return false;
    }

    auto composed = cue::math::compose(a_handler, *first.try_value(), *second.try_value(),
                                       a_tolerance);
    auto firstMatrix = cue::math::to_matrix4(a_handler, *first.try_value(), a_tolerance);
    auto secondMatrix = cue::math::to_matrix4(a_handler, *second.try_value(), a_tolerance);
    auto inverseMatrix = cue::math::inverse_matrix(a_handler, *first.try_value(), a_tolerance);

    if (!composed || !firstMatrix || !secondMatrix || !inverseMatrix)
    {
        return false;
    }

    const auto source = cue::math::Vector3{4.0F, 5.0F, 6.0F};
    const auto transformed = cue::math::transform_point(source, *firstMatrix.try_value());
    const auto restored = cue::math::transform_point(transformed, *inverseMatrix.try_value());
    return cue::math::is_near(*composed.try_value(),
                              *firstMatrix.try_value() * *secondMatrix.try_value(), a_tolerance) &&
           cue::math::is_near(restored, source, a_tolerance);
}

/// @brief Zero Scale TransformのForward変換を許可しInverseだけを拒否することを検証する
[[nodiscard]] bool test_transform_zero_scale(TestEmergencyHandler &a_handler,
                                             const cue::math::Tolerance &a_tolerance)
{
    auto transform = cue::math::Transform::create(
        a_handler, cue::math::Vector3{3.0F, 4.0F, 5.0F}, cue::math::Quaternion{},
        cue::math::Vector3{0.0F, 2.0F, 3.0F}, a_tolerance);

    if (!transform)
    {
        return false;
    }

    auto forward = cue::math::transform_point(
        a_handler, cue::math::Vector3{7.0F, 1.0F, 1.0F}, *transform.try_value(), a_tolerance);
    auto inverseResult = cue::math::inverse_matrix(a_handler, *transform.try_value(),
                                                   a_tolerance);

    return forward &&
           cue::math::is_near(*forward.try_value(), cue::math::Vector3{3.0F, 6.0F, 8.0F},
                              a_tolerance) &&
           has_math_error(inverseResult, 3);
}

/// @brief 非一様ScaleとRotationの合成がShearをMatrixへ保持しTRS分解を拒否することを検証する
[[nodiscard]] bool test_transform_shear_composition(TestEmergencyHandler &a_handler,
                                                    const cue::math::Tolerance &a_tolerance)
{
    auto rotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 0.0F, 1.0F},
        cue::math::Radians(cue::math::pi() * 0.25F), a_tolerance);

    if (!rotation)
    {
        return false;
    }

    auto first = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, *rotation.try_value(),
        cue::math::Vector3{1.0F, 1.0F, 1.0F}, a_tolerance);
    auto second = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, cue::math::Quaternion{},
        cue::math::Vector3{2.0F, 1.0F, 1.0F}, a_tolerance);

    if (!first || !second)
    {
        return false;
    }

    auto composed = cue::math::compose(a_handler, *first.try_value(), *second.try_value(),
                                       a_tolerance);

    if (!composed)
    {
        return false;
    }

    auto decomposed = cue::math::decompose(a_handler, *composed.try_value(), a_tolerance);
    return has_math_error(decomposed, 4);
}

/// @brief Transform Factoryが非有限値と非単位Rotationを分類して拒否することを検証する
[[nodiscard]] bool test_transform_factory_failures(TestEmergencyHandler &a_handler,
                                                   const cue::math::Tolerance &a_tolerance)
{
    const auto infinity = std::numeric_limits<float>::infinity();
    auto translation = cue::math::Transform::create(
        a_handler, cue::math::Vector3{infinity, 0.0F, 0.0F}, cue::math::Quaternion{},
        cue::math::Vector3{1.0F, 1.0F, 1.0F}, a_tolerance);
    auto scale = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, cue::math::Quaternion{},
        cue::math::Vector3{1.0F, infinity, 1.0F}, a_tolerance);
    auto rotation = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, cue::math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F},
        cue::math::Vector3{1.0F, 1.0F, 1.0F}, a_tolerance);

    return has_math_error(translation, 1) && has_math_error(scale, 1) &&
           has_math_error(rotation, 2);
}

/// @brief 有限入力からOverflowしたQuaternionとTransform演算を成功値として返さないことを検証する
[[nodiscard]] bool test_transform_overflow_failures(TestEmergencyHandler &a_handler,
                                                    const cue::math::Tolerance &a_tolerance)
{
    const auto maximum = std::numeric_limits<float>::max();
    auto scaleTwo = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, cue::math::Quaternion{},
        cue::math::Vector3{2.0F, 1.0F, 1.0F}, a_tolerance);
    auto scaleMaximum = cue::math::Transform::create(
        a_handler, cue::math::Vector3{}, cue::math::Quaternion{},
        cue::math::Vector3{maximum, 1.0F, 1.0F}, a_tolerance);
    auto rotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 0.0F, 1.0F},
        cue::math::Radians(cue::math::pi() * 0.25F), a_tolerance);

    if (!scaleTwo || !scaleMaximum || !rotation)
    {
        return false;
    }

    const auto extreme = cue::math::Vector3{maximum, maximum, 0.0F};
    auto point = cue::math::transform_point(a_handler, extreme, *scaleTwo.try_value(),
                                            a_tolerance);
    auto direction = cue::math::transform_direction(a_handler, extreme,
                                                    *scaleTwo.try_value(), a_tolerance);
    auto rotated = cue::math::rotate(a_handler, extreme, *rotation.try_value(), a_tolerance);
    auto composed = cue::math::compose(a_handler, *scaleMaximum.try_value(),
                                       *scaleTwo.try_value(), a_tolerance);

    return has_math_error(point, 1) && has_math_error(direction, 1) &&
           has_math_error(rotated, 1) && has_math_error(composed, 1);
}

/// @brief 負Scaleを含むTRS分解がX Axisへ符号を決定的に割り当て再構築できることを検証する
[[nodiscard]] bool test_transform_decomposition(TestEmergencyHandler &a_handler,
                                                const cue::math::Tolerance &a_tolerance)
{
    auto rotation = cue::math::from_axis_angle(
        a_handler, cue::math::Vector3{0.0F, 1.0F, 0.0F},
        cue::math::Radians(cue::math::pi() * 0.5F), a_tolerance);

    if (!rotation)
    {
        return false;
    }

    auto source = cue::math::Transform::create(
        a_handler, cue::math::Vector3{4.0F, 5.0F, 6.0F}, *rotation.try_value(),
        cue::math::Vector3{-2.0F, 3.0F, 4.0F}, a_tolerance);

    if (!source)
    {
        return false;
    }

    auto matrix = cue::math::to_matrix4(a_handler, *source.try_value(), a_tolerance);

    if (!matrix)
    {
        return false;
    }

    auto decomposed = cue::math::decompose(a_handler, *matrix.try_value(), a_tolerance);

    if (!decomposed)
    {
        return false;
    }

    auto reconstructed = cue::math::to_matrix4(a_handler, *decomposed.try_value(), a_tolerance);
    return reconstructed && decomposed.try_value()->scale().x < 0.0F &&
           cue::math::is_near(*reconstructed.try_value(), *matrix.try_value(), a_tolerance);
}

/// @brief Shear、Zero Scale、非Affine Matrixの分解が代替Transformを返さないことを検証する
[[nodiscard]] bool test_transform_decomposition_failures(
    TestEmergencyHandler &a_handler, const cue::math::Tolerance &a_tolerance)
{
    auto shear = cue::math::Matrix4{};
    shear.values[0][1] = 0.5F;
    auto zeroScale = cue::math::Matrix4{};
    zeroScale.values[1][1] = 0.0F;
    auto nonAffine = cue::math::Matrix4{};
    nonAffine.values[0][3] = 0.5F;
    auto shearResult = cue::math::decompose(a_handler, shear, a_tolerance);
    auto zeroScaleResult = cue::math::decompose(a_handler, zeroScale, a_tolerance);
    auto nonAffineResult = cue::math::decompose(a_handler, nonAffine, a_tolerance);

    return has_math_error(shearResult, 4) && has_math_error(zeroScaleResult, 4) &&
           has_math_error(nonAffineResult, 4);
}
} // namespace

/// @brief Cue.MathのMatrix、Quaternion、Transform契約を固定値と異常値で検証する
int main()
{
    auto handler = TestEmergencyHandler{};
    auto toleranceResult = cue::math::Tolerance::create(handler, 0.0F, 0.0F);

    if (!toleranceResult)
    {
        return 1;
    }

    auto tolerance = *toleranceResult.try_value();

    if (!create_test_tolerance(handler, tolerance))
    {
        return 1;
    }

    return test_matrix_identity_and_order(tolerance) && test_matrix_inverse(handler, tolerance) &&
                   test_matrix_inverse_failures(handler, tolerance) &&
                   test_matrix_inverse_scale_awareness(handler) &&
                   test_matrix_inverse_precision_failure(handler) &&
                   test_quaternion_basis(handler, tolerance) &&
                   test_quaternion_composition(handler, tolerance) &&
                   test_quaternion_failures(handler, tolerance) &&
                   test_transform_order(handler, tolerance) &&
                   test_transform_composition_and_inverse(handler, tolerance) &&
                   test_transform_zero_scale(handler, tolerance) &&
                   test_transform_shear_composition(handler, tolerance) &&
                   test_transform_factory_failures(handler, tolerance) &&
                   test_transform_overflow_failures(handler, tolerance) &&
                   test_transform_decomposition(handler, tolerance) &&
                   test_transform_decomposition_failures(handler, tolerance)
               ? 0
               : 1;
}
