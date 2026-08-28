#include <Cue/Math/Angle.h>
#include <Cue/Math/Vector.h>

#include <cmath>
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

/// @brief Testで共通利用する有限かつ非負のToleranceを生成する
[[nodiscard]] bool create_test_tolerance(TestEmergencyHandler &a_handler,
                                         cue::math::Tolerance &a_tolerance)
{
    auto result = cue::math::Tolerance::create(a_handler, 0.00001F, 0.00001F);

    if (!result)
    {
        return false;
    }

    a_tolerance = *result.try_value();
    return true;
}

/// @brief Toleranceが有限かつ非負の値だけを受理し、分類済みErrorを返すことを検証する
[[nodiscard]] bool test_tolerance_validation(TestEmergencyHandler &a_handler)
{
    auto valid = cue::math::Tolerance::create(a_handler, 0.0F, 0.01F);
    auto negative = cue::math::Tolerance::create(a_handler, -0.1F, 0.0F);
    auto nonFinite = cue::math::Tolerance::create(
        a_handler, std::numeric_limits<float>::infinity(), 0.0F);

    const auto *negativeError = negative.try_error();
    const auto *nonFiniteError = nonFinite.try_error();
    return valid && negativeError != nullptr && nonFiniteError != nullptr &&
           negativeError->code().domain() == "Cue.Math" &&
           negativeError->code().value() == 2 && nonFiniteError->code().value() == 1;
}

/// @brief RadianとDegreeが明示変換され、単位ごとの比較を維持することを検証する
[[nodiscard]] bool test_angle_conversion(const cue::math::Tolerance &a_tolerance)
{
    const auto radians = cue::math::to_radians(cue::math::Degrees(180.0F));
    const auto degrees = cue::math::to_degrees(cue::math::Radians(cue::math::pi() * 0.5F));

    return cue::math::is_near(radians, cue::math::Radians(cue::math::pi()), a_tolerance) &&
           cue::math::is_near(degrees, cue::math::Degrees(90.0F), a_tolerance) &&
           cue::math::Radians(1.0F) == cue::math::Radians(1.0F) &&
           cue::math::Degrees(1.0F) != cue::math::Degrees(2.0F);
}

/// @brief Vector2、Vector3、Vector4の算術演算と内積を固定値で検証する
[[nodiscard]] bool test_vector_arithmetic()
{
    const auto vector2 = cue::math::Vector2{1.0F, 2.0F} + cue::math::Vector2{3.0F, 4.0F};
    const auto vector3 = 2.0F * cue::math::Vector3{1.0F, -2.0F, 3.0F};
    const auto vector4 = cue::math::Vector4{8.0F, 6.0F, 4.0F, 2.0F} / 2.0F;

    return vector2 == cue::math::Vector2{4.0F, 6.0F} &&
           vector3 == cue::math::Vector3{2.0F, -4.0F, 6.0F} &&
           vector4 == cue::math::Vector4{4.0F, 3.0F, 2.0F, 1.0F} &&
           cue::math::dot(cue::math::Vector2{1.0F, 2.0F},
                          cue::math::Vector2{3.0F, 4.0F}) == 11.0F &&
           cue::math::dot(cue::math::Vector3{1.0F, 2.0F, 3.0F},
                          cue::math::Vector3{4.0F, 5.0F, 6.0F}) == 32.0F &&
           cue::math::dot(cue::math::Vector4{1.0F, 2.0F, 3.0F, 4.0F},
                          cue::math::Vector4{4.0F, 3.0F, 2.0F, 1.0F}) == 20.0F;
}

/// @brief 左手World規約でもCross Productが右手則の代数定義を維持することを検証する
[[nodiscard]] bool test_cross_product()
{
    const auto result = cue::math::cross(cue::math::Vector3{1.0F, 0.0F, 0.0F},
                                         cue::math::Vector3{0.0F, 1.0F, 0.0F});
    return result == cue::math::Vector3{0.0F, 0.0F, 1.0F};
}

/// @brief 大小の有限値でLength計算が中間OverflowとUnderflowを避けることを検証する
[[nodiscard]] bool test_scaled_length()
{
    const auto large = std::numeric_limits<float>::max() * 0.25F;
    const auto small = std::numeric_limits<float>::min();
    const auto largeLength = cue::math::length(cue::math::Vector2{large, large});
    const auto smallLength = cue::math::length(cue::math::Vector2{small, small});

    return std::isfinite(largeLength) && largeLength > large && smallLength > 0.0F &&
           cue::math::length(cue::math::Vector3{0.0F, 3.0F, 4.0F}) == 5.0F;
}

/// @brief 有限で十分な長さのVectorを正規化し、元の入力を変更しないことを検証する
[[nodiscard]] bool test_normalize_success(TestEmergencyHandler &a_handler,
                                          const cue::math::Tolerance &a_tolerance)
{
    const auto source = cue::math::Vector3{3.0F, 0.0F, 4.0F};
    auto result = cue::math::normalize(a_handler, source, a_tolerance);
    auto vector2 = cue::math::normalize(a_handler, cue::math::Vector2{0.0F, 2.0F},
                                        a_tolerance);
    auto vector4 = cue::math::normalize(a_handler, cue::math::Vector4{0.0F, 0.0F, 0.0F, 8.0F},
                                        a_tolerance);
    const auto maximum = std::numeric_limits<float>::max();
    auto extreme = cue::math::normalize(a_handler, cue::math::Vector2{maximum, maximum},
                                        a_tolerance);
    const auto *normalized = result.try_value();
    const auto *normalized2 = vector2.try_value();
    const auto *normalized4 = vector4.try_value();
    const auto *normalizedExtreme = extreme.try_value();

    return normalized != nullptr && source == cue::math::Vector3{3.0F, 0.0F, 4.0F} &&
           normalized2 != nullptr && normalized4 != nullptr && normalizedExtreme != nullptr &&
           cue::math::is_near(*normalized, cue::math::Vector3{0.6F, 0.0F, 0.8F},
                              a_tolerance) &&
           *normalized2 == cue::math::Vector2{0.0F, 1.0F} &&
           *normalized4 == cue::math::Vector4{0.0F, 0.0F, 0.0F, 1.0F} &&
           cue::math::is_near(cue::math::length(*normalized), 1.0F, a_tolerance) &&
           cue::math::is_near(cue::math::length(*normalizedExtreme), 1.0F, a_tolerance);
}

/// @brief Zero Lengthと非有限Vectorの正規化が代替値を返さず分類済みErrorになることを検証する
[[nodiscard]] bool test_normalize_failures(TestEmergencyHandler &a_handler,
                                           const cue::math::Tolerance &a_tolerance)
{
    auto boundaryToleranceResult = cue::math::Tolerance::create(a_handler, 1.0F, 0.0F);

    if (!boundaryToleranceResult)
    {
        return false;
    }

    const auto boundaryTolerance = *boundaryToleranceResult.try_value();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto infinity = std::numeric_limits<float>::infinity();

    auto vector2Zero = cue::math::normalize(a_handler, cue::math::Vector2{}, a_tolerance);
    auto vector2Below =
        cue::math::normalize(a_handler, cue::math::Vector2{0.5F, 0.0F}, boundaryTolerance);
    auto vector2Boundary =
        cue::math::normalize(a_handler, cue::math::Vector2{1.0F, 0.0F}, boundaryTolerance);
    auto vector2Above =
        cue::math::normalize(a_handler, cue::math::Vector2{1.0001F, 0.0F}, boundaryTolerance);
    auto vector2Nan = cue::math::normalize(a_handler, cue::math::Vector2{nan, 0.0F},
                                           a_tolerance);
    auto vector2Infinity = cue::math::normalize(
        a_handler, cue::math::Vector2{0.0F, infinity}, a_tolerance);

    auto vector3Zero = cue::math::normalize(a_handler, cue::math::Vector3{}, a_tolerance);
    auto vector3Below = cue::math::normalize(
        a_handler, cue::math::Vector3{0.0F, 0.5F, 0.0F}, boundaryTolerance);
    auto vector3Boundary = cue::math::normalize(
        a_handler, cue::math::Vector3{0.0F, 1.0F, 0.0F}, boundaryTolerance);
    auto vector3Above = cue::math::normalize(
        a_handler, cue::math::Vector3{0.0F, 1.0001F, 0.0F}, boundaryTolerance);
    auto vector3Nan = cue::math::normalize(a_handler, cue::math::Vector3{0.0F, nan, 0.0F},
                                           a_tolerance);
    auto vector3Infinity = cue::math::normalize(
        a_handler, cue::math::Vector3{0.0F, 0.0F, infinity}, a_tolerance);

    auto vector4Zero = cue::math::normalize(a_handler, cue::math::Vector4{}, a_tolerance);
    auto vector4Below = cue::math::normalize(
        a_handler, cue::math::Vector4{0.0F, 0.0F, 0.5F, 0.0F}, boundaryTolerance);
    auto vector4Boundary = cue::math::normalize(
        a_handler, cue::math::Vector4{0.0F, 0.0F, 1.0F, 0.0F}, boundaryTolerance);
    auto vector4Above = cue::math::normalize(
        a_handler, cue::math::Vector4{0.0F, 0.0F, 1.0001F, 0.0F}, boundaryTolerance);
    auto vector4Nan = cue::math::normalize(
        a_handler, cue::math::Vector4{0.0F, 0.0F, nan, 0.0F}, a_tolerance);
    auto vector4Infinity = cue::math::normalize(
        a_handler, cue::math::Vector4{0.0F, 0.0F, 0.0F, infinity}, a_tolerance);

    return has_math_error(vector2Zero, 2) && has_math_error(vector2Below, 2) &&
           has_math_error(vector2Boundary, 2) && vector2Above &&
           has_math_error(vector2Nan, 1) && has_math_error(vector2Infinity, 1) &&
           has_math_error(vector3Zero, 2) && has_math_error(vector3Below, 2) &&
           has_math_error(vector3Boundary, 2) && vector3Above &&
           has_math_error(vector3Nan, 1) && has_math_error(vector3Infinity, 1) &&
           has_math_error(vector4Zero, 2) && has_math_error(vector4Below, 2) &&
           has_math_error(vector4Boundary, 2) && vector4Above &&
           has_math_error(vector4Nan, 1) && has_math_error(vector4Infinity, 1);
}

/// @brief 完全一致と近似比較がNaN、Signed Zero、有限値を規定どおり扱うことを検証する
[[nodiscard]] bool test_comparison(const cue::math::Tolerance &a_tolerance)
{
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    const auto nanVector = cue::math::Vector2{nan, 0.0F};

    return cue::math::Vector2{0.0F, -0.0F} == cue::math::Vector2{-0.0F, 0.0F} &&
           nanVector != nanVector && !cue::math::is_near(nanVector, nanVector, a_tolerance) &&
           cue::math::is_near(cue::math::Vector2{1000.0F, 1.0F},
                              cue::math::Vector2{1000.005F, 1.0F}, a_tolerance);
}

/// @brief 極端な有限値の比較で中間Overflowによる誤一致が発生しないことを検証する
[[nodiscard]] bool test_comparison_overflow(TestEmergencyHandler &a_handler)
{
    auto narrowResult = cue::math::Tolerance::create(a_handler, 0.0F, 1.5F);
    auto boundaryResult = cue::math::Tolerance::create(a_handler, 0.0F, 2.0F);

    if (!narrowResult || !boundaryResult)
    {
        return false;
    }

    const auto maximum = std::numeric_limits<float>::max();
    return !cue::math::is_near(maximum, -maximum, *narrowResult.try_value()) &&
           cue::math::is_near(maximum, -maximum, *boundaryResult.try_value());
}
} // namespace

/// @brief Cue.MathのScalar、Angle、Vector正常系と異常系を検証して終了Codeを返す
int main()
{
    static_assert(sizeof(float) == 4);
    static_assert(std::numeric_limits<float>::is_iec559);

    auto handler = TestEmergencyHandler{};
    auto tolerance = cue::math::Tolerance::create(handler, 0.0F, 0.0F);

    if (!tolerance)
    {
        return 1;
    }

    auto testTolerance = *tolerance.try_value();

    if (!create_test_tolerance(handler, testTolerance))
    {
        return 1;
    }

    return test_tolerance_validation(handler) && test_angle_conversion(testTolerance) &&
                   test_vector_arithmetic() && test_cross_product() && test_scaled_length() &&
                   test_normalize_success(handler, testTolerance) &&
                   test_normalize_failures(handler, testTolerance) &&
                   test_comparison(testTolerance) && test_comparison_overflow(handler)
               ? 0
               : 1;
}
