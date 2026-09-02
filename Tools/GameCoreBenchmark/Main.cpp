#include <Cue/Foundation/Assert.h>
#include <Cue/Foundation/Fatal.h>
#include <Cue/Foundation/Log.h>
#include <Cue/Foundation/NumberParsing.h>
#include <Cue/Foundation/Result.h>
#include <Cue/GameCore/CommandBuffer.h>
#include <Cue/GameCore/World.h>
#include <Cue/Schema/Descriptor.h>
#include <Cue/Schema/Registry.h>
#include <Cue/Schema/Types.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
constexpr std::uint64_t k_maxEntityCount = 10'000'000;
constexpr std::uint64_t k_maxRunCount = 1'000;

constexpr std::string_view k_positionId = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view k_velocityId = "30000000-0000-4000-8000-000000000003";

class BenchmarkFatalHandler final : public cue::FatalHandler
{
  public:
    /// @brief Benchmark 内の通常 Fatal を失敗 Process として終了する
    [[noreturn]] void terminate() noexcept override
    {
        std::abort();
    }

    /// @brief Logger を利用できない Fatal も失敗 Process として終了する
    [[noreturn]] void terminate(std::string_view) noexcept override
    {
        std::abort();
    }
};

struct Position final
{
    float x;
    float y;
    float z;

    /// @brief 測定対象となる座標 Component 値を構築する
    Position(float a_x, float a_y, float a_z) noexcept : x(a_x), y(a_y), z(a_z)
    {
    }
};

struct Velocity final
{
    float x;
    float y;
    float z;

    /// @brief 二 Component Query の速度入力を構築する
    Velocity(float a_x, float a_y, float a_z) noexcept : x(a_x), y(a_y), z(a_z)
    {
    }
};

struct BenchmarkOptions final
{
    std::uint64_t entityCount = 10'000;
    std::uint32_t warmupCount = 3;
    std::uint32_t iterationCount = 10;
    std::filesystem::path outputPath;
};

struct BenchmarkResult final
{
    std::string name;
    std::uint64_t operationCount = 0;
    std::uint64_t aggregateChecksum = 0;
    double medianNanoseconds = 0.0;
    double p95Nanoseconds = 0.0;
    double operationsPerSecond = 0.0;
};

/// @brief Result の成功値を所有権ごと取り出し、Benchmark 前提違反を例外へ変換する
template <typename T> [[nodiscard]] T require_value(cue::Result<T> &&a_result)
{
    T *value = a_result.try_value();

    if (value == nullptr)
    {
        throw std::runtime_error("CueEngine operation failed");
    }

    return std::move(*value);
}

/// @brief 値を返さない Result の成功を要求して不完全な Fixture を拒否する
void require_success(cue::Result<void> &&a_result)
{
    if (!a_result)
    {
        throw std::runtime_error("CueEngine operation failed");
    }
}

/// @brief Benchmark Fixture 用の検証済み TypeId を生成する
[[nodiscard]] cue::schema::TypeId make_type_id(std::string_view a_text, const cue::AssertContext &a_assertContext)
{
    return require_value(cue::schema::TypeId::parse(a_text, a_assertContext));
}

/// @brief Field を持たない比較用 Type Descriptor を生成する
[[nodiscard]] cue::schema::TypeDescriptor make_type(std::string_view a_typeId, std::string_view a_name,
                                                    const cue::AssertContext &a_assertContext)
{
    auto version = require_value(cue::schema::SchemaVersion::create(1U, a_assertContext));
    std::vector<cue::schema::FieldDescriptor> fields;
    std::vector<cue::schema::FieldId> reservedFieldIds;
    return require_value(cue::schema::create_type_descriptor(make_type_id(a_typeId, a_assertContext), a_name,
                                                             std::move(version), std::move(fields),
                                                             std::move(reservedFieldIds), a_assertContext));
}

/// @brief Position と Velocity の Stable Schema を固定順序で Seal する
[[nodiscard]] std::unique_ptr<cue::schema::SchemaRegistry> make_registry(
    cue::schema::SchemaRegistryIdentitySource &a_identitySource, const cue::AssertContext &a_assertContext)
{
    cue::schema::SchemaRegistryBuilder builder(a_identitySource, a_assertContext);
    require_success(builder.add_type(make_type(k_positionId, "Cue.Benchmark.Position", a_assertContext)));
    require_success(builder.add_type(make_type(k_velocityId, "Cue.Benchmark.Velocity", a_assertContext)));
    return require_value(builder.seal());
}

class BenchmarkEnvironment final
{
  public:
    /// @brief 全 Sample より長寿命な診断、Schema、World Identity 依存を所有する
    BenchmarkEnvironment()
        : m_logger(m_fatalHandler, std::vector<std::unique_ptr<cue::LogSink>>{}),
          m_assertContext(m_logger, m_fatalHandler), m_registry(make_registry(m_schemaIdentitySource, m_assertContext))
    {
    }

    /// @brief Fixture が共有する Seal 済み Schema Registry を返す
    [[nodiscard]] const cue::schema::SchemaRegistry &registry() const noexcept
    {
        return *m_registry;
    }

    /// @brief 全 World と Capability より長寿命な World Identity Source を返す
    [[nodiscard]] cue::game_core::WorldIdentitySource &world_identity() noexcept
    {
        return m_worldIdentitySource;
    }

    /// @brief Engine API の失敗診断へ共有する Assert Context を返す
    [[nodiscard]] const cue::AssertContext &assert_context() const noexcept
    {
        return m_assertContext;
    }

  private:
    BenchmarkFatalHandler m_fatalHandler;
    cue::Logger m_logger;
    cue::AssertContext m_assertContext;
    cue::schema::SchemaRegistryIdentitySource m_schemaIdentitySource;
    std::unique_ptr<cue::schema::SchemaRegistry> m_registry;
    cue::game_core::WorldIdentitySource m_worldIdentitySource;
};

class BenchmarkFixture final
{
  public:
    /// @brief 一 Sample 専用 World と Component Capability を構築する
    explicit BenchmarkFixture(BenchmarkEnvironment &a_environment)
        : world(require_value(cue::game_core::World::create(a_environment.world_identity(), a_environment.registry(),
                                                            a_environment.assert_context())))
    {
        positionType.emplace(require_value(
            world->register_component<Position>(make_type_id(k_positionId, a_environment.assert_context()))));
        velocityType.emplace(require_value(
            world->register_component<Velocity>(make_type_id(k_velocityId, a_environment.assert_context()))));
    }

    /// @brief Position Capability が Fixture 構築済みであることを保証して返す
    [[nodiscard]] cue::game_core::ComponentType<Position> position_type() const
    {
        return *positionType;
    }

    /// @brief Velocity Capability が Fixture 構築済みであることを保証して返す
    [[nodiscard]] cue::game_core::ComponentType<Velocity> velocity_type() const
    {
        return *velocityType;
    }

    std::unique_ptr<cue::game_core::World> world;
    std::optional<cue::game_core::ComponentType<Position>> positionType;
    std::optional<cue::game_core::ComponentType<Velocity>> velocityType;
    std::vector<cue::game_core::EntityHandle> entities;
    std::unique_ptr<cue::game_core::StructuralCommandBuffer> commandBuffer;
    std::uint64_t checksum = 0;
};

using FixtureAction = std::function<void(BenchmarkFixture &)>;

/// @brief 正の範囲へ制限した Command Line の符号なし整数を解析する
[[nodiscard]] bool parse_count(std::string_view a_text, std::uint64_t a_maximum, bool a_allowsZero,
                               std::uint64_t &a_value) noexcept
{
    const auto parsed = cue::parse_unsigned_decimal<std::uint64_t>(a_text);

    if (!parsed.has_value() || *parsed > a_maximum || (!a_allowsZero && *parsed == 0))
    {
        return false;
    }

    a_value = *parsed;
    return true;
}

/// @brief 比較条件を固定する Command Line Option を解析する
[[nodiscard]] bool parse_options(int a_argumentCount, char **a_arguments, BenchmarkOptions &a_options)
{
    for (int index = 1; index < a_argumentCount; ++index)
    {
        if (index + 1 >= a_argumentCount)
        {
            return false;
        }

        const std::string_view argument = a_arguments[index];
        const std::string_view value = a_arguments[++index];
        std::uint64_t parsed = 0;

        if (argument == "--entities")
        {
            if (!parse_count(value, k_maxEntityCount, false, parsed))
            {
                return false;
            }
            a_options.entityCount = parsed;
        }
        else if (argument == "--warmup")
        {
            if (!parse_count(value, k_maxRunCount, true, parsed))
            {
                return false;
            }
            a_options.warmupCount = static_cast<std::uint32_t>(parsed);
        }
        else if (argument == "--iterations")
        {
            if (!parse_count(value, k_maxRunCount, false, parsed))
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

/// @brief 指定数の Entity を生成して後続 Workload 用の同一集合を保持する
void generate_entities(BenchmarkFixture &a_fixture, std::uint64_t a_entityCount)
{
    a_fixture.entities.clear();
    a_fixture.entities.reserve(static_cast<std::size_t>(a_entityCount));

    for (std::uint64_t index = 0; index < a_entityCount; ++index)
    {
        a_fixture.entities.push_back(require_value(a_fixture.world->create_entity()));
    }
}

/// @brief 全 Entity へ Position を追加して同一 Component Storage 状態を作る
void add_positions(BenchmarkFixture &a_fixture)
{
    for (const auto entity : a_fixture.entities)
    {
        const float value = static_cast<float>(entity.index());
        static_cast<void>(require_value(
            a_fixture.world->add_component(a_fixture.position_type(), entity, value, value + 1.0F, value + 2.0F)));
    }
}

/// @brief 全 Entity へ Velocity を追加して二 Component Query 入力を作る
void add_velocities(BenchmarkFixture &a_fixture)
{
    for (const auto entity : a_fixture.entities)
    {
        const float value = static_cast<float>((entity.index() % 7U) + 1U);
        static_cast<void>(
            require_value(a_fixture.world->add_component(a_fixture.velocity_type(), entity, value, 2.0F, 3.0F)));
    }
}

/// @brief Warm-up 後の Sample から中央値と p95 を同じ算出規則で求める
[[nodiscard]] BenchmarkResult measure_workload(std::string a_name, std::uint64_t a_operationCount,
                                               BenchmarkEnvironment &a_environment, const BenchmarkOptions &a_options,
                                               const FixtureAction &a_prepare, const FixtureAction &a_execute)
{
    std::vector<double> samples;
    samples.reserve(a_options.iterationCount);
    std::uint64_t aggregateChecksum = 0;
    const std::uint32_t totalRuns = a_options.warmupCount + a_options.iterationCount;

    for (std::uint32_t runIndex = 0; runIndex < totalRuns; ++runIndex)
    {
        BenchmarkFixture fixture(a_environment);
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
    const std::size_t middle = samples.size() / 2U;
    const double median = samples.size() % 2U == 0U ? (samples[middle - 1U] + samples[middle]) * 0.5 : samples[middle];
    const std::size_t p95Index = static_cast<std::size_t>(std::ceil(static_cast<double>(samples.size()) * 0.95)) - 1U;

    BenchmarkResult result;
    result.name = std::move(a_name);
    result.operationCount = a_operationCount;
    result.aggregateChecksum = aggregateChecksum;
    result.medianNanoseconds = median;
    result.p95Nanoseconds = samples[p95Index];
    result.operationsPerSecond = median > 0.0 ? static_cast<double>(a_operationCount) * 1'000'000'000.0 / median : 0.0;
    return result;
}

/// @brief Rebuild ECS の七つの比較 Workload を旧 Baseline と同じ順序で実行する
[[nodiscard]] std::vector<BenchmarkResult> run_benchmarks(BenchmarkEnvironment &a_environment,
                                                          const BenchmarkOptions &a_options)
{
    std::vector<BenchmarkResult> results;
    results.reserve(7U);

    /// @brief Entity 生成 Workload に追加 Setup が不要であることを表す
    const FixtureAction prepareEmpty = [](BenchmarkFixture &) {};
    /// @brief Entity 生成だけを測定して最後の Slot を検証値へ残す
    const FixtureAction generate = [&a_options](BenchmarkFixture &a_fixture)
    {
        generate_entities(a_fixture, a_options.entityCount);
        a_fixture.checksum = a_fixture.entities.back().index();
    };
    results.push_back(
        measure_workload("entity_generate", a_options.entityCount, a_environment, a_options, prepareEmpty, generate));

    /// @brief Entity 再利用以降の Workload へ生成済み集合を用意する
    const FixtureAction prepareEntities = [&a_options](BenchmarkFixture &a_fixture)
    { generate_entities(a_fixture, a_options.entityCount); };
    /// @brief 全 Entity の破棄と同数再生成を測定する
    const FixtureAction recycle = [&a_options](BenchmarkFixture &a_fixture)
    {
        for (const auto entity : a_fixture.entities)
        {
            require_success(a_fixture.world->destroy_entity(entity));
        }
        generate_entities(a_fixture, a_options.entityCount);
        a_fixture.checksum = a_fixture.entities.back().generation();
    };
    results.push_back(measure_workload("entity_destroy_reuse", a_options.entityCount * 2U, a_environment, a_options,
                                       prepareEntities, recycle));

    /// @brief 全 Entity への Position 追加だけを測定する
    const FixtureAction addPosition = [](BenchmarkFixture &a_fixture)
    {
        add_positions(a_fixture);
        auto *component =
            require_value(a_fixture.world->get_component(a_fixture.position_type(), a_fixture.entities.back()));
        a_fixture.checksum = static_cast<std::uint64_t>(component->x);
    };
    results.push_back(measure_workload("component_add", a_options.entityCount, a_environment, a_options,
                                       prepareEntities, addPosition));

    /// @brief Component 取得と削除の Workload へ Position 付き集合を用意する
    const FixtureAction preparePositions = [&a_options](BenchmarkFixture &a_fixture)
    {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
    };
    /// @brief 全 Entity の Position を順次取得して値を消費する
    const FixtureAction getPosition = [](BenchmarkFixture &a_fixture)
    {
        double sum = 0.0;
        for (const auto entity : a_fixture.entities)
        {
            auto *component = require_value(a_fixture.world->get_component(a_fixture.position_type(), entity));
            sum += component->x;
        }
        a_fixture.checksum = static_cast<std::uint64_t>(sum);
    };
    results.push_back(measure_workload("component_get_sequential", a_options.entityCount, a_environment, a_options,
                                       preparePositions, getPosition));

    /// @brief 全 Entity の Position を一つずつ削除する
    const FixtureAction removePosition = [](BenchmarkFixture &a_fixture)
    {
        for (const auto entity : a_fixture.entities)
        {
            require_success(a_fixture.world->remove_component(a_fixture.position_type(), entity));
        }
        a_fixture.checksum = a_fixture.world->entity_count();
    };
    results.push_back(measure_workload("component_remove", a_options.entityCount, a_environment, a_options,
                                       preparePositions, removePosition));

    /// @brief 二 Component Query へ Position と Velocity の交差集合を用意する
    const FixtureAction prepareQuery = [&a_options](BenchmarkFixture &a_fixture)
    {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
        add_velocities(a_fixture);
    };
    /// @brief Position と Velocity の交差集合を一度走査して値を更新する
    const FixtureAction queryTwoComponents = [](BenchmarkFixture &a_fixture)
    {
        /// @brief Query が各 Entity の Position を Velocity で更新する
        auto update = [](cue::game_core::EntityHandle, Position &a_position, Velocity &a_velocity) noexcept
        {
            a_position.x += a_velocity.x;
            a_position.y += a_velocity.y;
            a_position.z += a_velocity.z;
        };
        const std::size_t count =
            require_value(a_fixture.world->query_write(a_fixture.position_type(), a_fixture.velocity_type(), update));
        auto *component =
            require_value(a_fixture.world->get_component(a_fixture.position_type(), a_fixture.entities.back()));
        a_fixture.checksum = static_cast<std::uint64_t>(component->x) ^ count;
    };
    results.push_back(measure_workload("query_two_components", a_options.entityCount, a_environment, a_options,
                                       prepareQuery, queryTwoComponents));

    /// @brief Safe Point 測定へ Position 削除 Command を全 Entity 分記録する
    const FixtureAction prepareDeferred = [&a_options](BenchmarkFixture &a_fixture)
    {
        generate_entities(a_fixture, a_options.entityCount);
        add_positions(a_fixture);
        a_fixture.commandBuffer = std::make_unique<cue::game_core::StructuralCommandBuffer>(*a_fixture.world);
        for (const auto entity : a_fixture.entities)
        {
            require_success(a_fixture.commandBuffer->remove_component(a_fixture.position_type(), entity));
        }
    };
    /// @brief 記録済み削除 Command を FIFO で一括反映する時間を測定する
    const FixtureAction applyDeferred = [](BenchmarkFixture &a_fixture)
    {
        auto report = a_fixture.world->flush_commands(*a_fixture.commandBuffer);
        const auto commandResults = report.results();
        std::uint64_t succeeded = 0;
        for (const auto &result : commandResults)
        {
            succeeded += result.succeeded() ? 1U : 0U;
        }
        if (succeeded != commandResults.size())
        {
            throw std::runtime_error("Deferred command failed");
        }
        a_fixture.checksum = succeeded;
    };
    results.push_back(measure_workload("deferred_component_remove", a_options.entityCount, a_environment, a_options,
                                       prepareDeferred, applyDeferred));

    return results;
}

/// @brief Benchmark 結果を Machine 処理可能な JSON として保存する
[[nodiscard]] bool write_results(const BenchmarkOptions &a_options, const std::vector<BenchmarkResult> &a_results)
{
    std::ofstream output(a_options.outputPath, std::ios::binary | std::ios::trunc);
    if (!output)
    {
        return false;
    }

    output << "{\n"
           << "    \"schemaVersion\": 1,\n"
           << "    \"implementation\": \"rebuild-m10\",\n"
           << "    \"rebuildCommit\": \"" << CUE_REBUILD_COMMIT << "\",\n"
           << "    \"clock\": \"std::chrono::steady_clock\",\n"
           << "    \"entityCount\": " << a_options.entityCount << ",\n"
           << "    \"warmupCount\": " << a_options.warmupCount << ",\n"
           << "    \"iterationCount\": " << a_options.iterationCount << ",\n"
           << "    \"results\": [\n";

    for (std::size_t index = 0; index < a_results.size(); ++index)
    {
        const BenchmarkResult &result = a_results[index];
        output << "        {\"name\": \"" << result.name << "\", \"operationCount\": " << result.operationCount
               << ", \"aggregateChecksum\": " << result.aggregateChecksum
               << ", \"medianNanoseconds\": " << result.medianNanoseconds
               << ", \"p95Nanoseconds\": " << result.p95Nanoseconds
               << ", \"operationsPerSecond\": " << result.operationsPerSecond << "}";
        output << (index + 1U == a_results.size() ? "\n" : ",\n");
    }

    output << "    ]\n}\n";
    return output.good();
}
} // namespace

/// @brief Rebuild ECS の比較条件を解析し、七つの Baseline を保存する
int main(int a_argumentCount, char **a_arguments)
{
    try
    {
        BenchmarkOptions options;
        if (!parse_options(a_argumentCount, a_arguments, options) || options.outputPath.empty())
        {
            std::cerr << "Usage: CueGameCoreBenchmark --entities <count> "
                         "--warmup <count> --iterations <count> --output <path>\n";
            return 2;
        }

        BenchmarkEnvironment environment;
        const auto results = run_benchmarks(environment, options);
        if (!write_results(options, results))
        {
            std::cerr << "Failed to write benchmark results\n";
            return 3;
        }

        std::cout << "Rebuild ECS baseline completed: " << options.outputPath.string() << '\n';
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Rebuild ECS baseline failed: " << error.what() << '\n';
        return 4;
    }
}
