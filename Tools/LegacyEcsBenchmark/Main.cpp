#include <ECSManager.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{
struct Position final : Cue::ECS::IComponentTag
{
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct Velocity final : Cue::ECS::IComponentTag
{
    float x = 1.0F;
    float y = 2.0F;
    float z = 3.0F;
};

struct BenchmarkOptions final
{
    std::uint64_t entityCount = 10'000;
    std::uint32_t warmupCount = 3;
    std::uint32_t iterationCount = 10;
    std::filesystem::path outputPath;
};

constexpr std::uint64_t k_maxEntityCount = 10'000'000;
constexpr std::uint64_t k_maxRunCount = 1'000;

struct BenchmarkResult final
{
    std::string name;
    std::uint64_t operationCount = 0;
    double medianNanoseconds = 0.0;
    double p95Nanoseconds = 0.0;
    double operationsPerSecond = 0.0;
};

class BenchmarkClock final : public Cue::Core::Time::IClock
{
public:
    /// @brief 旧 ECS の内部計測へ単調増加する Nanosecond Clockを提供する
    [[nodiscard]] Cue::Math::TimeSpan now_ns() const noexcept override
    {
        const auto now = std::chrono::steady_clock::now().time_since_epoch();
        const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
        return {nanoseconds, Cue::Math::TimeUnit::nanoseconds};
    }
};

struct LegacyFixture final
{
    BenchmarkClock clock;
    Cue::ECS::ECSManager manager{clock};
    std::vector<Cue::ECS::Entity> entities;
    std::uint64_t checksum = 0;
};

using FixtureAction = std::function<void(LegacyFixture &)>;

/// @brief 符号なし整数引数を範囲検査付きで解析する
[[nodiscard]] bool parse_unsigned(std::string_view a_text, std::uint64_t &a_value) noexcept
{
    if (a_text.empty())
    {
        return false;
    }

    std::uint64_t parsed = 0;
    const char *const begin = a_text.data();
    const char *const end = begin + a_text.size();
    const auto [position, error] = std::from_chars(begin, end, parsed);

    if (error != std::errc{} || position != end)
    {
        return false;
    }

    a_value = parsed;
    return true;
}

/// @brief 比較条件を固定するCommand Line Optionを解析する
[[nodiscard]] bool parse_options(int a_argumentCount, char **a_arguments, BenchmarkOptions &a_options)
{
    for (int index = 1; index < a_argumentCount; ++index)
    {
        const std::string_view argument = a_arguments[index];

        if (index + 1 >= a_argumentCount)
        {
            return false;
        }

        const std::string_view value = a_arguments[++index];
        std::uint64_t parsed = 0;

        if (argument == "--entities")
        {
            if (!parse_unsigned(value, parsed) || parsed == 0 || parsed > k_maxEntityCount)
            {
                return false;
            }
            a_options.entityCount = parsed;
        }
        else if (argument == "--warmup")
        {
            if (!parse_unsigned(value, parsed) || parsed > k_maxRunCount)
            {
                return false;
            }
            a_options.warmupCount = static_cast<std::uint32_t>(parsed);
        }
        else if (argument == "--iterations")
        {
            if (!parse_unsigned(value, parsed) || parsed == 0 || parsed > k_maxRunCount)
            {
                return false;
            }
            a_options.iterationCount = static_cast<std::uint32_t>(parsed);
        }
        else if (argument == "--output")
        {
            a_options.outputPath = std::filesystem::path(value);
        }
        else
        {
            return false;
        }
    }

    return true;
}

/// @brief 指定数のEntityを生成し、後続Workloadが同じ集合を使用できるよう保持する
void generate_entities(LegacyFixture &a_fixture, std::uint64_t a_entityCount)
{
    a_fixture.entities.clear();
    a_fixture.entities.reserve(static_cast<std::size_t>(a_entityCount));

    for (std::uint64_t index = 0; index < a_entityCount; ++index)
    {
        a_fixture.entities.push_back(a_fixture.manager.generate_entity());
    }
}

/// @brief 全EntityへPositionを追加し、Component Storageを比較可能な同一状態へする
void add_positions(LegacyFixture &a_fixture)
{
    for (const Cue::ECS::Entity entity : a_fixture.entities)
    {
        Position *const position = a_fixture.manager.add_component<Position>(entity);
        position->x = static_cast<float>(entity);
    }
}

/// @brief 全EntityへVelocityを追加し、複数Component Queryの入力を構築する
void add_velocities(LegacyFixture &a_fixture)
{
    for (const Cue::ECS::Entity entity : a_fixture.entities)
    {
        Velocity *const velocity = a_fixture.manager.add_component<Velocity>(entity);
        velocity->x = static_cast<float>((entity % 7U) + 1U);
    }
}

/// @brief Archetype走査時にComponent値を更新し、処理が最適化で除去されない結果を残す
void update_position(Cue::ECS::Entity, Position &a_position, Velocity &a_velocity)
{
    a_position.x += a_velocity.x;
    a_position.y += a_velocity.y;
    a_position.z += a_velocity.z;
}

/// @brief Warm-up後の複数Sampleから中央値とp95を計算する
[[nodiscard]] BenchmarkResult measure_workload(std::string a_name, std::uint64_t a_operationCount,
                                               const BenchmarkOptions &a_options, const FixtureAction &a_prepare,
                                               const FixtureAction &a_execute)
{
    std::vector<double> samples;
    samples.reserve(a_options.iterationCount);

    const std::uint32_t totalRuns = a_options.warmupCount + a_options.iterationCount;
    std::uint64_t aggregateChecksum = 0;

    for (std::uint32_t runIndex = 0; runIndex < totalRuns; ++runIndex)
    {
        LegacyFixture fixture;
        a_prepare(fixture);

        const auto begin = std::chrono::steady_clock::now();
        a_execute(fixture);
        const auto end = std::chrono::steady_clock::now();
        aggregateChecksum ^= fixture.checksum;

        if (runIndex >= a_options.warmupCount)
        {
            samples.push_back(std::chrono::duration<double, std::nano>(end - begin).count());
        }
    }

    std::sort(samples.begin(), samples.end());
    const std::size_t middle = samples.size() / 2;
    const double median = samples.size() % 2 == 0 ? (samples[middle - 1] + samples[middle]) * 0.5 : samples[middle];
    const std::size_t p95Index = static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1;

    BenchmarkResult result;
    result.name = std::move(a_name);
    result.operationCount = a_operationCount;
    result.medianNanoseconds = median;
    result.p95Nanoseconds = samples[p95Index];
    result.operationsPerSecond = median > 0.0 ? static_cast<double>(a_operationCount) * 1'000'000'000.0 / median : 0.0;

    if (aggregateChecksum == UINT64_MAX)
    {
        std::cerr << "checksum guard\n";
    }

    return result;
}

/// @brief 旧ECSの比較対象Workloadを固定順序で実行する
[[nodiscard]] std::vector<BenchmarkResult> run_benchmarks(const BenchmarkOptions &a_options)
{
    std::vector<BenchmarkResult> results;
    results.reserve(7);

    /// @brief Entity生成Workloadの空Fixtureを用意する
    const FixtureAction prepareEmpty = [](LegacyFixture &) {};
    /// @brief Entity生成時間を測定する
    const FixtureAction generate = [&a_options](LegacyFixture &a_fixture) {
        generate_entities(a_fixture, a_options.entityCount);
        a_fixture.checksum = a_fixture.entities.back();
    };
    results.push_back(measure_workload("entity_generate", a_options.entityCount, a_options, prepareEmpty, generate));

    /// @brief Entity再利用Workloadへ生成済みEntityを用意する
    const FixtureAction prepareEntities = [&a_options](LegacyFixture &a_fixture) {
        generate_entities(a_fixture, a_options.entityCount);
    };
    /// @brief 全Entityの破棄と同数再生成を測定する
    const FixtureAction recycle = [&a_options](LegacyFixture &a_fixture) {
        for (const Cue::ECS::Entity entity : a_fixture.entities)
        {
            a_fixture.manager.remove_entity(entity);
        }
        generate_entities(a_fixture, a_options.entityCount);
        a_fixture.checksum = a_fixture.entities.back();
    };
    results.push_back(
        measure_workload("entity_destroy_reuse", a_options.entityCount * 2, a_options, prepareEntities, recycle));

    /// @brief Position追加時間を測定する
    const FixtureAction addPosition = [](LegacyFixture &a_fixture) {
        add_positions(a_fixture);
        a_fixture.checksum = static_cast<std::uint64_t>(a_fixture.manager.get_component<Position>(a_fixture.entities.back())->x);
    };
    results.push_back(
        measure_workload("component_add", a_options.entityCount, a_options, prepareEntities, addPosition));

    /// @brief Component取得WorkloadへPosition付きEntityを用意する
    const FixtureAction preparePositions = [&a_options](LegacyFixture &a_fixture) {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
    };
    /// @brief 全EntityのPosition取得時間を測定する
    const FixtureAction getPosition = [](LegacyFixture &a_fixture) {
        double sum = 0.0;
        for (const Cue::ECS::Entity entity : a_fixture.entities)
        {
            sum += a_fixture.manager.get_component<Position>(entity)->x;
        }
        a_fixture.checksum = static_cast<std::uint64_t>(sum);
    };
    results.push_back(
        measure_workload("component_get_sequential", a_options.entityCount, a_options, preparePositions, getPosition));

    /// @brief 全EntityのPosition削除時間を測定する
    const FixtureAction removePosition = [](LegacyFixture &a_fixture) {
        for (const Cue::ECS::Entity entity : a_fixture.entities)
        {
            a_fixture.manager.remove_component<Position>(entity);
        }
        a_fixture.checksum = a_fixture.manager.get_component<Position>(a_fixture.entities.back()) == nullptr ? 1U : 0U;
    };
    results.push_back(
        measure_workload("component_remove", a_options.entityCount, a_options, preparePositions, removePosition));

    /// @brief Query Workloadへ2 ComponentとSystemを用意する
    const FixtureAction prepareQuery = [&a_options](LegacyFixture &a_fixture) {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
        add_velocities(a_fixture);
        using MovementSystem = Cue::ECS::ECSManager::System<Position, Velocity>;
        a_fixture.manager.add_system<MovementSystem>(
            std::function<void(Cue::ECS::Entity, Position &, Velocity &)>{update_position});
    };
    /// @brief 2 Component条件のSystem走査時間を測定する
    const FixtureAction queryTwoComponents = [](LegacyFixture &a_fixture) {
        a_fixture.manager.update_all_systems();
        a_fixture.checksum =
            static_cast<std::uint64_t>(a_fixture.manager.get_component<Position>(a_fixture.entities.back())->x);
    };
    results.push_back(measure_workload("query_two_components", a_options.entityCount, a_options, prepareQuery,
                                       queryTwoComponents));

    /// @brief Deferred Mutation WorkloadへPosition付きEntityとCommand Queueを用意する
    const FixtureAction prepareDeferred = [&a_options](LegacyFixture &a_fixture) {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
        for (const Cue::ECS::Entity entity : a_fixture.entities)
        {
            /// @brief Safe PointでPositionを削除する旧ECS Commandを登録する
            a_fixture.manager.defer([manager = &a_fixture.manager, entity]() {
                manager->remove_component<Position>(entity);
            });
        }
    };
    /// @brief 空System更新後にDeferred Commandを一括反映する時間を測定する
    const FixtureAction applyDeferred = [](LegacyFixture &a_fixture) {
        a_fixture.manager.update_all_systems();
        a_fixture.checksum = a_fixture.manager.get_component<Position>(a_fixture.entities.back()) == nullptr ? 1U : 0U;
    };
    results.push_back(measure_workload("deferred_component_remove", a_options.entityCount, a_options, prepareDeferred,
                                       applyDeferred));

    return results;
}

/// @brief Benchmark結果をMachine処理可能なJSONへ保存する
[[nodiscard]] bool write_results(const BenchmarkOptions &a_options, const std::vector<BenchmarkResult> &a_results)
{
    std::ofstream output(a_options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }

    output << "{\n"
           << "    \"schemaVersion\": 1,\n"
           << "    \"implementation\": \"legacy-release\",\n"
           << "    \"legacyCommit\": \"" << CUE_LEGACY_COMMIT << "\",\n"
           << "    \"clock\": \"std::chrono::steady_clock\",\n"
           << "    \"entityCount\": " << a_options.entityCount << ",\n"
           << "    \"warmupCount\": " << a_options.warmupCount << ",\n"
           << "    \"iterationCount\": " << a_options.iterationCount << ",\n"
           << "    \"results\": [\n";

    for (std::size_t index = 0; index < a_results.size(); ++index)
    {
        const BenchmarkResult &result = a_results[index];
        output << "        {\"name\": \"" << result.name << "\", \"operationCount\": " << result.operationCount
               << ", \"medianNanoseconds\": " << result.medianNanoseconds << ", \"p95Nanoseconds\": "
               << result.p95Nanoseconds << ", \"operationsPerSecond\": " << result.operationsPerSecond << "}";
        output << (index + 1 == a_results.size() ? "\n" : ",\n");
    }

    output << "    ]\n}\n";
    return output.good();
}
} // namespace

/// @brief 比較条件を解析し、旧ECS Workloadを実行してBaseline結果を保存する
int main(int a_argumentCount, char **a_arguments)
{
    try
    {
        BenchmarkOptions options;
        if (!parse_options(a_argumentCount, a_arguments, options) || options.outputPath.empty())
        {
            std::cerr << "Usage: CueLegacyEcsBenchmark --entities <count> --warmup <count> --iterations <count> "
                         "--output <path>\n";
            return 2;
        }

        const std::vector<BenchmarkResult> results = run_benchmarks(options);
        if (!write_results(options, results))
        {
            std::cerr << "Failed to write benchmark results\n";
            return 3;
        }

        std::cout << "Legacy ECS baseline completed: " << options.outputPath.string() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Legacy ECS baseline failed: " << error.what() << '\n';
        return 4;
    }
}
