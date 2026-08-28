#include <Cue/Math/Matrix.h>

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
/// @brief Math DomainのMatrix Errorを生成する
[[nodiscard]] cue::Error make_matrix_error(cue::EmergencyHandler &a_emergencyHandler,
                                           std::int64_t a_code,
                                           std::string_view a_summary) noexcept
{
    auto errorCode = cue::ErrorCode::create(a_emergencyHandler, "Cue.Math", a_code);
    return cue::Error::create(a_emergencyHandler, std::move(errorCode), a_summary);
}

/// @brief Scale付き部分Pivot選択で有限なSquare Matrixを反転する
template <std::size_t Size, typename Matrix>
[[nodiscard]] cue::Result<Matrix> invert_matrix(cue::EmergencyHandler &a_emergencyHandler,
                                                 const Matrix &a_value,
                                                 const Tolerance &a_tolerance) noexcept
{
    double augmented[Size][Size * 2]{};
    double rowScales[Size]{};

    for (std::size_t row = 0; row < Size; ++row)
    {
        for (std::size_t column = 0; column < Size; ++column)
        {
            const auto value = a_value.values[row][column];

            if (!is_finite(value))
            {
                auto error = make_matrix_error(a_emergencyHandler, 1,
                                               "Matrix elements must be finite");
                return cue::Result<Matrix>::failure(std::move(error));
            }

            augmented[row][column] = static_cast<double>(value);
            augmented[row][column + Size] = row == column ? 1.0 : 0.0;
            rowScales[row] = std::max(rowScales[row], std::abs(augmented[row][column]));
        }
    }

    for (std::size_t pivotColumn = 0; pivotColumn < Size; ++pivotColumn)
    {
        auto pivotRow = pivotColumn;
        auto bestRatio = -1.0;
        auto pivotScale = 0.0;

        for (std::size_t row = pivotColumn; row < Size; ++row)
        {
            const auto ratio = rowScales[row] == 0.0
                                   ? 0.0
                                   : std::abs(augmented[row][pivotColumn]) / rowScales[row];

            if (ratio > bestRatio)
            {
                bestRatio = ratio;
                pivotRow = row;
                pivotScale = rowScales[row];
            }
        }

        const auto pivotMagnitude = std::abs(augmented[pivotRow][pivotColumn]);
        const auto allowedPivot =
            std::max(static_cast<double>(a_tolerance.absolute()),
                     static_cast<double>(a_tolerance.relative()) * pivotScale);

        if (pivotMagnitude <= allowedPivot)
        {
            auto error = make_matrix_error(a_emergencyHandler, 3,
                                           "Matrix is singular within the requested tolerance");
            return cue::Result<Matrix>::failure(std::move(error));
        }

        if (pivotRow != pivotColumn)
        {
            std::swap(rowScales[pivotRow], rowScales[pivotColumn]);

            for (std::size_t column = 0; column < Size * 2; ++column)
            {
                std::swap(augmented[pivotRow][column], augmented[pivotColumn][column]);
            }
        }

        const auto pivot = augmented[pivotColumn][pivotColumn];

        for (std::size_t column = 0; column < Size * 2; ++column)
        {
            augmented[pivotColumn][column] /= pivot;
        }

        for (std::size_t row = 0; row < Size; ++row)
        {
            if (row == pivotColumn)
            {
                continue;
            }

            const auto factor = augmented[row][pivotColumn];

            for (std::size_t column = 0; column < Size * 2; ++column)
            {
                augmented[row][column] -= factor * augmented[pivotColumn][column];
            }
        }
    }

    auto result = Matrix{};

    for (std::size_t row = 0; row < Size; ++row)
    {
        for (std::size_t column = 0; column < Size; ++column)
        {
            const auto value = static_cast<float>(augmented[row][column + Size]);

            if (!is_finite(value))
            {
                auto error = make_matrix_error(a_emergencyHandler, 3,
                                               "Matrix inverse is not representable as float");
                return cue::Result<Matrix>::failure(std::move(error));
            }

            result.values[row][column] = value;
        }
    }

    const auto identity = Matrix{};

    if (!is_near(a_value * result, identity, a_tolerance) ||
        !is_near(result * a_value, identity, a_tolerance))
    {
        auto error = make_matrix_error(
            a_emergencyHandler, 3,
            "Matrix inverse does not restore identity within the requested tolerance");
        return cue::Result<Matrix>::failure(std::move(error));
    }

    return cue::Result<Matrix>::success(std::move(result));
}
} // namespace

/// @brief 全要素がZeroのMatrix3を返す
Matrix3 zero_matrix3() noexcept
{
    auto result = Matrix3{};

    for (auto &row : result.values)
    {
        for (auto &value : row)
        {
            value = 0.0F;
        }
    }

    return result;
}

/// @brief 全要素がZeroのMatrix4を返す
Matrix4 zero_matrix4() noexcept
{
    auto result = Matrix4{};

    for (auto &row : result.values)
    {
        for (auto &value : row)
        {
            value = 0.0F;
        }
    }

    return result;
}

/// @brief Matrix3を組込みfloatの完全一致規則で比較する
bool operator==(const Matrix3 &a_left, const Matrix3 &a_right) noexcept
{
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (a_left.values[row][column] != a_right.values[row][column])
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix3が完全一致しない場合にtrueを返す
bool operator!=(const Matrix3 &a_left, const Matrix3 &a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Matrix4を組込みfloatの完全一致規則で比較する
bool operator==(const Matrix4 &a_left, const Matrix4 &a_right) noexcept
{
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (a_left.values[row][column] != a_right.values[row][column])
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix4が完全一致しない場合にtrueを返す
bool operator!=(const Matrix4 &a_left, const Matrix4 &a_right) noexcept
{
    return !(a_left == a_right);
}

/// @brief Matrix3を時間順に合成する
Matrix3 operator*(const Matrix3 &a_left, const Matrix3 &a_right) noexcept
{
    auto result = zero_matrix3();

    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            for (std::size_t index = 0; index < 3; ++index)
            {
                result.values[row][column] +=
                    a_left.values[row][index] * a_right.values[index][column];
            }
        }
    }

    return result;
}

/// @brief Matrix4を時間順に合成する
Matrix4 operator*(const Matrix4 &a_left, const Matrix4 &a_right) noexcept
{
    auto result = zero_matrix4();

    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            for (std::size_t index = 0; index < 4; ++index)
            {
                result.values[row][column] +=
                    a_left.values[row][index] * a_right.values[index][column];
            }
        }
    }

    return result;
}

/// @brief 行VectorをMatrix3で変換する
Vector3 operator*(Vector3 a_value, const Matrix3 &a_matrix) noexcept
{
    return {
        a_value.x * a_matrix.values[0][0] + a_value.y * a_matrix.values[1][0] +
            a_value.z * a_matrix.values[2][0],
        a_value.x * a_matrix.values[0][1] + a_value.y * a_matrix.values[1][1] +
            a_value.z * a_matrix.values[2][1],
        a_value.x * a_matrix.values[0][2] + a_value.y * a_matrix.values[1][2] +
            a_value.z * a_matrix.values[2][2],
    };
}

/// @brief Matrix3の全要素を指定Tolerance内で比較する
bool is_near(const Matrix3 &a_left, const Matrix3 &a_right,
             const Tolerance &a_tolerance) noexcept
{
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            if (!is_near(a_left.values[row][column], a_right.values[row][column],
                         a_tolerance))
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix4の全要素を指定Tolerance内で比較する
bool is_near(const Matrix4 &a_left, const Matrix4 &a_right,
             const Tolerance &a_tolerance) noexcept
{
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            if (!is_near(a_left.values[row][column], a_right.values[row][column],
                         a_tolerance))
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix3の全要素が有限な場合にtrueを返す
bool is_finite(const Matrix3 &a_value) noexcept
{
    for (const auto &row : a_value.values)
    {
        for (const auto value : row)
        {
            if (!is_finite(value))
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix4の全要素が有限な場合にtrueを返す
bool is_finite(const Matrix4 &a_value) noexcept
{
    for (const auto &row : a_value.values)
    {
        for (const auto value : row)
        {
            if (!is_finite(value))
            {
                return false;
            }
        }
    }

    return true;
}

/// @brief Matrix4でPointを変換し、平行移動を適用する
Vector3 transform_point(Vector3 a_value, const Matrix4 &a_matrix) noexcept
{
    return {
        a_value.x * a_matrix.values[0][0] + a_value.y * a_matrix.values[1][0] +
            a_value.z * a_matrix.values[2][0] + a_matrix.values[3][0],
        a_value.x * a_matrix.values[0][1] + a_value.y * a_matrix.values[1][1] +
            a_value.z * a_matrix.values[2][1] + a_matrix.values[3][1],
        a_value.x * a_matrix.values[0][2] + a_value.y * a_matrix.values[1][2] +
            a_value.z * a_matrix.values[2][2] + a_matrix.values[3][2],
    };
}

/// @brief Matrix4でDirectionを変換し、平行移動を適用しない
Vector3 transform_direction(Vector3 a_value, const Matrix4 &a_matrix) noexcept
{
    return {
        a_value.x * a_matrix.values[0][0] + a_value.y * a_matrix.values[1][0] +
            a_value.z * a_matrix.values[2][0],
        a_value.x * a_matrix.values[0][1] + a_value.y * a_matrix.values[1][1] +
            a_value.z * a_matrix.values[2][1],
        a_value.x * a_matrix.values[0][2] + a_value.y * a_matrix.values[1][2] +
            a_value.z * a_matrix.values[2][2],
    };
}

/// @brief 有限かつ非特異なMatrix3の逆Matrixを返す
cue::Result<Matrix3> inverse(cue::EmergencyHandler &a_emergencyHandler,
                             const Matrix3 &a_value,
                             const Tolerance &a_tolerance) noexcept
{
    return invert_matrix<3>(a_emergencyHandler, a_value, a_tolerance);
}

/// @brief 有限かつ非特異なMatrix4の逆Matrixを返す
cue::Result<Matrix4> inverse(cue::EmergencyHandler &a_emergencyHandler,
                             const Matrix4 &a_value,
                             const Tolerance &a_tolerance) noexcept
{
    return invert_matrix<4>(a_emergencyHandler, a_value, a_tolerance);
}
} // namespace cue::math
