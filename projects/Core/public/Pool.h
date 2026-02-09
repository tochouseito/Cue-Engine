#pragma once
#include <vector>
#include <memory>
#include <utility>

namespace Cue::Core
{
    template<typename T, typename ResetFunc>
    class Pool
    {
    public:
        struct Deleter
        {
            Pool* pool = nullptr;

            void operator()(T* p) const noexcept
            {
                // 1) pool が無ければ普通に delete
                if (pool == nullptr)
                {
                    delete p;
                    return;
                }

                // 2) pool に返却
                pool->release(std::unique_ptr<T>(p));
            }
        };

        using pooled_ptr = std::unique_ptr<T, Deleter>;

    public:
        Pool(size_t maxCached, ResetFunc resetFunc)
            : m_maxCached(maxCached)
            , m_resetFunc(std::move(resetFunc))
        {
        }

        Pool(const Pool&) = delete;
        Pool& operator=(const Pool&) = delete;

    public:
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
            T* raw = new T();
            ++m_totalAllocated;
            return pooled_ptr(raw, Deleter{ this });
        }

        void prewarm(size_t totalCount)
        {
            // 1) 総確保数が totalCount になるまで作る
            while (m_totalAllocated < totalCount)
            {
                if (m_cached.size() >= m_maxCached)
                {
                    return;
                }

                m_cached.push_back(std::make_unique<T>());
                ++m_totalAllocated;
            }
        }

        size_t cached_count() const noexcept
        {
            return m_cached.size();
        }

        size_t total_allocated() const noexcept
        {
            return m_totalAllocated;
        }

    private:
        void release(std::unique_ptr<T> obj) noexcept
        {
            // 1) 状態リセット
            m_resetFunc(*obj);

            // 2) キャッシュ上限超過なら捨てる
            if (m_cached.size() >= m_maxCached)
            {
                return;
            }

            // 3) 戻す（LIFO）
            m_cached.push_back(std::move(obj));
        }

    private:
        size_t m_maxCached = 0;
        ResetFunc m_resetFunc{};
        std::vector<std::unique_ptr<T>> m_cached{};
        size_t m_totalAllocated = 0;
    };
    // 何もしないリセット（デフォルト用）
    template <typename T>
    struct NoReset
    {
        void operator()(T&) const noexcept
        {
            // 1) no-op
        }
    };
}
