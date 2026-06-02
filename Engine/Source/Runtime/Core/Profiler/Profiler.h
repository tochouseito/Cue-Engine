// Profiler の役割と公開要素を定義する

#pragma once

// === C++ includes ===
#include <chrono>
#include <cstdint>
#include <vector>

namespace Cue::Core::Profiler
{
    /// @brief 登録済み計測点を参照する軽量ハンドル
    struct EntryHandle final
    {
        uint32_t value = UINT32_MAX;

        [[nodiscard]] bool is_valid() const noexcept
        {
            return value != UINT32_MAX;
        }
    };

    /// @brief 表示やログ出力に使うフレーム計測結果
    struct EntrySnapshot final
    {
        EntryHandle entry{};
        const char* category = "";
        const char* name = "";
        uint64_t callCount = 0;
        double totalMs = 0.0;
        double averageMs = 0.0;
        double maxMs = 0.0;
        double lastMs = 0.0;
    };

    /// @brief 計測点を登録し、以後の計測で使うハンドルを返す
    [[nodiscard]] EntryHandle register_entry(
        const char* a_category,
        const char* a_name);

    /// @brief フレーム境界を進め、直前フレームの計測結果を固定する
    void begin_frame();

    /// @brief 登録済み計測点の計測値を初期化する
    void reset();

    /// @brief 計測点の有効状態を切り替える
    void set_entry_enabled(EntryHandle a_entry, bool a_isEnabled);

    /// @brief 計測点が有効なら `true` を返す
    [[nodiscard]] bool is_entry_enabled(EntryHandle a_entry);

    /// @brief ナノ秒単位の計測値を登録済み計測点へ加算する
    void record_sample_ns(EntryHandle a_entry, uint64_t a_elapsedNs);

    /// @brief 直前フレームの計測結果を収集する
    [[nodiscard]] std::vector<EntrySnapshot> collect_frame_snapshots();

    /// @brief スコープ寿命で計測する RAII オブジェクト
    class ScopedSample final
    {
    public:
        explicit ScopedSample(EntryHandle a_entry);
        ~ScopedSample();

        ScopedSample(const ScopedSample&) = delete;
        ScopedSample& operator=(const ScopedSample&) = delete;

        ScopedSample(ScopedSample&&) = delete;
        ScopedSample& operator=(ScopedSample&&) = delete;

    private:
        using Clock = std::chrono::steady_clock;

        EntryHandle m_entry{};
        Clock::time_point m_startedAt{};
        bool m_isActive = false;
    };
}

#if !defined(CUE_ENABLE_PROFILER)
#ifdef CUE_DEBUG
#define CUE_ENABLE_PROFILER 1
#else
#define CUE_ENABLE_PROFILER 0
#endif
#endif

#define CUE_PROFILE_JOIN_INNER(a_left, a_right) a_left##a_right
#define CUE_PROFILE_JOIN(a_left, a_right) \
    CUE_PROFILE_JOIN_INNER(a_left, a_right)

#if defined(_MSC_VER)
#define CUE_PROFILE_FUNCTION_NAME __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
#define CUE_PROFILE_FUNCTION_NAME __PRETTY_FUNCTION__
#else
#define CUE_PROFILE_FUNCTION_NAME __func__
#endif

#if CUE_ENABLE_PROFILER
#define CUE_PROFILE_SCOPE(a_category, a_name)                                  \
    static const ::Cue::Core::Profiler::EntryHandle CUE_PROFILE_JOIN(          \
        cueProfileEntry,                                                       \
        __LINE__) = ::Cue::Core::Profiler::register_entry(                     \
        (a_category),                                                          \
        (a_name));                                                             \
    ::Cue::Core::Profiler::ScopedSample CUE_PROFILE_JOIN(                      \
        cueProfileSample,                                                      \
        __LINE__)(CUE_PROFILE_JOIN(cueProfileEntry, __LINE__))

#define CUE_PROFILE_FUNCTION(a_category) \
    CUE_PROFILE_SCOPE((a_category), CUE_PROFILE_FUNCTION_NAME)
#else
#define CUE_PROFILE_SCOPE(a_category, a_name) \
    do                                        \
    {                                         \
    }                                         \
    while (false)
#define CUE_PROFILE_FUNCTION(a_category) \
    do                                   \
    {                                    \
    }                                    \
    while (false)
#endif
