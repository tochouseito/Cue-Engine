#include <Cue/Math/Transform.h>

#include <cstddef>
#include <cstdlib>
#include <string_view>

namespace
{
class TestEmergencyHandler final : public cue::EmergencyHandler
{
  public:
    /// @brief Property Test中に予期しないEmergency終了経路へ到達した場合はProcessを異常終了する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

/// @brief 再現可能なProperty入力をIndexから生成する
[[nodiscard]] float sample_value(std::size_t a_index, std::size_t a_stride,
                                 float a_scale) noexcept
{
    const auto sample = static_cast<int>((a_index * a_stride + 17U) % 101U) - 50;
    return static_cast<float>(sample) * a_scale;
}

/// @brief Matrix変換を使わずHamilton積だけでQuaternion同士を乗算する
[[nodiscard]] cue::math::Quaternion hamilton_product(cue::math::Quaternion a_left,
                                                      cue::math::Quaternion a_right) noexcept
{
    return cue::math::Quaternion{
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

/// @brief Matrix変換を使わずQuaternionのSandwich積でVector3を回転する
[[nodiscard]] cue::math::Vector3 rotate_with_hamilton_product(
    cue::math::Vector3 a_value, cue::math::Quaternion a_rotation) noexcept
{
    const auto value = cue::math::Quaternion{a_value.x, a_value.y, a_value.z, 0.0F};
    const auto conjugate = cue::math::Quaternion{
        -a_rotation.x,
        -a_rotation.y,
        -a_rotation.z,
        a_rotation.w,
    };
    const auto rotated = hamilton_product(
        hamilton_product(a_rotation, value), conjugate);
    return cue::math::Vector3{rotated.x, rotated.y, rotated.z};
}

/// @brief 複数のAxisと角度でQuaternion回転がMatrix回転と一致することを検証する
[[nodiscard]] bool test_quaternion_matrix_agreement(
    TestEmergencyHandler &a_handler, const cue::math::Tolerance &a_tolerance)
{
    for (std::size_t index = 0; index < 96U; ++index)
    {
        const auto axis = cue::math::Vector3{
            1.0F + static_cast<float>(index % 5U),
            1.0F + static_cast<float>((index * 3U) % 7U),
            1.0F + static_cast<float>((index * 5U) % 11U),
        };
        const auto angle = cue::math::Radians(sample_value(index, 13U, 0.01F));
        const auto source = cue::math::Vector3{
            sample_value(index, 7U, 0.1F),
            sample_value(index, 11U, 0.1F),
            sample_value(index, 17U, 0.1F),
        };
        auto rotation = cue::math::from_axis_angle(a_handler, axis, angle, a_tolerance);

        if (!rotation)
        {
            return false;
        }

        auto matrix = cue::math::to_matrix3(a_handler, *rotation.try_value(), a_tolerance);
        auto rotated = cue::math::rotate(a_handler, source, *rotation.try_value(), a_tolerance);
        const auto independentlyRotated =
            rotate_with_hamilton_product(source, *rotation.try_value());

        if (!matrix || !rotated ||
            !cue::math::is_near(independentlyRotated, source * *matrix.try_value(),
                                a_tolerance) ||
            !cue::math::is_near(independentlyRotated, *rotated.try_value(), a_tolerance))
        {
            return false;
        }
    }

    return true;
}

/// @brief 複数の可逆TRSでMatrixの左右InverseとPoint往復が成立することを検証する
[[nodiscard]] bool test_transform_inverse_properties(
    TestEmergencyHandler &a_handler, const cue::math::Tolerance &a_tolerance)
{
    for (std::size_t index = 0; index < 96U; ++index)
    {
        const auto axis = cue::math::Vector3{
            1.0F + static_cast<float>(index % 3U),
            2.0F + static_cast<float>(index % 5U),
            3.0F + static_cast<float>(index % 7U),
        };
        auto rotation = cue::math::from_axis_angle(
            a_handler, axis, cue::math::Radians(sample_value(index, 19U, 0.005F)),
            a_tolerance);

        if (!rotation)
        {
            return false;
        }

        const auto translation = cue::math::Vector3{
            sample_value(index, 7U, 0.2F),
            sample_value(index, 13U, 0.2F),
            sample_value(index, 23U, 0.2F),
        };
        const auto scale = cue::math::Vector3{
            0.5F + static_cast<float>(index % 9U) * 0.25F,
            0.75F + static_cast<float>((index * 3U) % 9U) * 0.2F,
            1.0F + static_cast<float>((index * 5U) % 9U) * 0.15F,
        };
        const auto source = cue::math::Vector3{
            sample_value(index, 29U, 0.1F),
            sample_value(index, 31U, 0.1F),
            sample_value(index, 37U, 0.1F),
        };
        auto transform = cue::math::Transform::create(
            a_handler, translation, *rotation.try_value(), scale, a_tolerance);

        if (!transform)
        {
            return false;
        }

        auto matrix = cue::math::to_matrix4(a_handler, *transform.try_value(), a_tolerance);
        auto inverse = cue::math::inverse_matrix(a_handler, *transform.try_value(), a_tolerance);

        if (!matrix || !inverse ||
            !cue::math::is_near(*matrix.try_value() * *inverse.try_value(),
                                cue::math::Matrix4{}, a_tolerance) ||
            !cue::math::is_near(*inverse.try_value() * *matrix.try_value(),
                                cue::math::Matrix4{}, a_tolerance))
        {
            return false;
        }

        const auto transformed = cue::math::transform_point(source, *matrix.try_value());
        const auto restored = cue::math::transform_point(transformed, *inverse.try_value());

        if (!cue::math::is_near(restored, source, a_tolerance))
        {
            return false;
        }
    }

    return true;
}

/// @brief 複数のShearを含まないTRSで分解と再構築が元のMatrixを復元することを検証する
[[nodiscard]] bool test_transform_decomposition_properties(
    TestEmergencyHandler &a_handler, const cue::math::Tolerance &a_tolerance)
{
    for (std::size_t index = 0; index < 64U; ++index)
    {
        auto axis = cue::math::Vector3{
            1.0F + static_cast<float>(index % 4U),
            1.0F + static_cast<float>((index * 3U) % 6U),
            1.0F + static_cast<float>((index * 7U) % 8U),
        };
        const auto angle = index < 3U
                               ? cue::math::Radians(cue::math::pi())
                               : cue::math::Radians(
                                     sample_value(index, 41U, 0.07F));

        if (index == 0U)
        {
            axis = cue::math::Vector3{8.0F, 1.0F, 1.0F};
        }
        else if (index == 1U)
        {
            axis = cue::math::Vector3{1.0F, 8.0F, 1.0F};
        }
        else if (index == 2U)
        {
            axis = cue::math::Vector3{1.0F, 1.0F, 8.0F};
        }

        auto rotation = cue::math::from_axis_angle(
            a_handler, axis, angle, a_tolerance);

        if (!rotation)
        {
            return false;
        }

        auto transform = cue::math::Transform::create(
            a_handler,
            cue::math::Vector3{sample_value(index, 5U, 0.1F),
                               sample_value(index, 9U, 0.1F),
                               sample_value(index, 15U, 0.1F)},
            *rotation.try_value(),
            cue::math::Vector3{0.75F + static_cast<float>(index % 5U) * 0.25F,
                               1.0F + static_cast<float>(index % 7U) * 0.2F,
                               1.25F + static_cast<float>(index % 9U) * 0.15F},
            a_tolerance);

        if (!transform)
        {
            return false;
        }

        auto matrix = cue::math::to_matrix4(a_handler, *transform.try_value(), a_tolerance);

        if (!matrix)
        {
            return false;
        }

        auto decomposed = cue::math::decompose(a_handler, *matrix.try_value(), a_tolerance);

        if (!decomposed)
        {
            return false;
        }

        auto rebuilt = cue::math::to_matrix4(a_handler, *decomposed.try_value(), a_tolerance);

        if (!rebuilt ||
            !cue::math::is_near(*rebuilt.try_value(), *matrix.try_value(), a_tolerance))
        {
            return false;
        }
    }

    return true;
}
} // namespace

/// @brief Cue.Mathの主要な演算不変条件を再現可能な反復入力で検証する
int main()
{
    TestEmergencyHandler handler;
    auto tolerance = cue::math::Tolerance::create(handler, 0.0005F, 0.0005F);

    if (!tolerance)
    {
        return EXIT_FAILURE;
    }

    const auto testTolerance = *tolerance.try_value();
    return test_quaternion_matrix_agreement(handler, testTolerance) &&
                   test_transform_inverse_properties(handler, testTolerance) &&
                   test_transform_decomposition_properties(handler, testTolerance)
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}
