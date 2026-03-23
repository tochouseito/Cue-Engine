#pragma once

// === C++ includes ===
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace Cue::Core
{
    /// @brief 再利用可能なオブジェクトを保持するプールです。
    /// @tparam T 管理対象の型です。
    /// @tparam ResetFunc 返却時の状態リセット関数です。
    template <typename T, typename ResetFunc>
    class Pool
    {
    public:
        using create_func = std::function<std::unique_ptr<T>()>;

        struct Deleter
        {
            Pool* pool = nullptr;

            void operator()(T* a_ptr) const noexcept
            {
                // 1) pool が無ければ普通に delete
                if (pool == nullptr)
                {
                    delete a_ptr;
                    return;
                }

                // 2) pool に返却
                pool->release(std::unique_ptr<T>(a_ptr));
            }
        };

        using pooled_ptr = std::unique_ptr<T, Deleter>;

    public:
        /// @brief デフォルト生成関数付きのプールを構築します。
        /// @param a_maxCached キャッシュする最大個数です。
        /// @param a_resetFunc 返却時のリセット関数です。
        Pool(size_t a_maxCached, ResetFunc a_resetFunc)
            : Pool(
                a_maxCached,
                std::move(a_resetFunc),
                []()
                {
                    return std::make_unique<T>();
                })
        {}

        /// @brief 生成関数を指定してプールを構築します。
        /// @param a_maxCached キャッシュする最大個数です。
        /// @param a_resetFunc 返却時のリセット関数です。
        /// @param a_createFunc 新規生成関数です。
        Pool(size_t a_maxCached, ResetFunc a_resetFunc, create_func a_createFunc)
            : m_maxCached(a_maxCached)
            , m_resetFunc(std::move(a_resetFunc))
            , m_createFunc(std::move(a_createFunc))
        {}

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

    public:
        /// @brief オブジェクトを取得します。
        /// @return プール返却付きのスマートポインタです。
        pooled_ptr acquire()
        {
            // 1) キャッシュがあれば取り出す
            if (!m_cached.empty())
            {
                auto obj = std::move(m_cached.back());
                m_cached.pop_back();
                return pooled_ptr(obj.release(), Deleter{ this });
            }

            // 2) なければ新規確保
            std::unique_ptr<T> obj = m_createFunc();
            ++m_totalAllocated;
            return pooled_ptr(obj.release(), Deleter{ this });
        }

        /// @brief 生ポインタをプールへ戻します。
        /// @param a_raw 非所有の返却対象です。
        void recycle(T* a_raw) noexcept
        {
            if (a_raw == nullptr)
            {
                return;
            }

            release(std::unique_ptr<T>(a_raw));
        }

        /// @brief 指定数まで事前確保します。
        /// @param a_totalCount 総確保数の目標値です。
        void prewarm(size_t a_totalCount)
        {
            // 1) 総確保数が a_totalCount になるまで作る
            while (m_totalAllocated < a_totalCount)
            {
                if (m_cached.size() >= m_maxCached)
                {
                    return;
                }

                m_cached.push_back(m_createFunc());
                ++m_totalAllocated;
            }
        }

        /// @brief 現在キャッシュされている個数を返します。
        /// @return キャッシュ済み個数です。
        size_t cached_count() const noexcept
        {
            return m_cached.size();
        }

        /// @brief これまでに確保した総数を返します。
        /// @return 総確保数です。
        size_t total_allocated() const noexcept
        {
            return m_totalAllocated;
        }

    private:
        void release(std::unique_ptr<T> a_object) noexcept
        {
            // 1) 状態リセット
            m_resetFunc(*a_object);

            // 2) キャッシュ上限超過なら捨てる
            if (m_cached.size() >= m_maxCached)
            {
                return;
            }

            // 3) 戻す（LIFO）
            m_cached.push_back(std::move(a_object));
        }

    private:
        size_t m_maxCached = 0;
        ResetFunc m_resetFunc{};
        create_func m_createFunc{};
        std::vector<std::unique_ptr<T>> m_cached{};
        size_t m_totalAllocated = 0;
    };

    /// @brief 返却時に何もしない既定リセットです。
    template <typename T>
    struct NoReset
    {
        void operator()(T&) const noexcept
        {
            // 1) 既定動作では状態変更を行わない
        }
    };
}
