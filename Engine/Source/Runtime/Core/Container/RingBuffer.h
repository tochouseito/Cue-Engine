#pragma once

/// ************************************************************************************
/// RingBuffer
/// ************************************************************************************

// === C++ includes ===
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>

namespace Cue::Core
{
    /// @brief バイト単位のリング領域を順序どおりに再利用するアロケータ
    class RingBuffer final
    {
    public:
        struct Allocation final
        {
            size_t offset = 0;
            size_t size = 0;
            size_t reservedSize = 0;
            uint64_t id = 0;

            [[nodiscard]] bool valid() const noexcept
            {
                // - 発行済み ID を持つ割り当てだけを有効として扱う
                return id != 0;
            }
        };

    public:
        RingBuffer() = default;
        explicit RingBuffer(size_t a_capacity)
        {
            initialize(a_capacity);
        }

        void initialize(size_t a_capacity) noexcept
        {
            // - 新しい容量へ切り替えるときは、保持中の割り当て状態を全て破棄する
            m_capacity = a_capacity;
            clear();
        }

        void clear() noexcept
        {
            // - FIFO 解放待ちの履歴を捨てて、空バッファ状態へ戻す
            m_allocations.clear();
            m_tail = 0;
            m_used = 0;
        }

        [[nodiscard]] bool allocate(size_t a_size, size_t a_alignment, Allocation& a_outAllocation) noexcept
        {
            // - 無効な要求や容量超過を先に弾き、内部状態を壊さない
            a_outAllocation = {};
            if (m_capacity == 0 || a_size == 0)
            {
                return false;
            }

            const size_t alignment = (std::max)(a_alignment, static_cast<size_t>(1));
            if (a_size > m_capacity || free_size() < a_size)
            {
                return false;
            }

            const size_t head = head_offset();

            // - 末尾側へそのまま置けるなら、wrap を避けて連続領域を優先利用する
            if (m_allocations.empty() || m_tail >= head)
            {
                const size_t alignedTail = align_up(m_tail, alignment);
                const size_t padding = alignedTail - m_tail;
                if (alignedTail + a_size <= m_capacity && m_used + padding + a_size <= m_capacity)
                {
                    return commit_allocation(alignedTail, a_size, padding + a_size, a_outAllocation);
                }

                // - 末尾に入らない場合だけ先頭へ巻き戻し、末尾スラックごと予約済みとして扱う
                if (a_size <= head && m_used + (m_capacity - m_tail) + a_size <= m_capacity)
                {
                    return commit_allocation(0, a_size, (m_capacity - m_tail) + a_size, a_outAllocation);
                }

                return false;
            }

            // - 既に wrap 済みなら head の手前だけが自由領域なので、その範囲に収まるかだけを見る
            const size_t alignedTail = align_up(m_tail, alignment);
            const size_t padding = alignedTail - m_tail;
            if (alignedTail + a_size > head || m_used + padding + a_size > m_capacity)
            {
                return false;
            }

            return commit_allocation(alignedTail, a_size, padding + a_size, a_outAllocation);
        }

        [[nodiscard]] bool release(const Allocation& a_allocation) noexcept
        {
            // - FIFO の先頭以外を解放するとリングが壊れるので、順序違反は拒否する
            if (m_allocations.empty() || !a_allocation.valid())
            {
                return false;
            }

            const Allocation& front = m_allocations.front();
            if (front.id != a_allocation.id)
            {
                return false;
            }

            // - 予約時に消費したバイト数を戻し、次の先頭割り当てが head になる状態へ進める
            m_used -= front.reservedSize;
            m_allocations.pop_front();
            if (m_allocations.empty())
            {
                m_tail = 0;
            }

            return true;
        }

        [[nodiscard]] bool empty() const noexcept
        {
            // - アクティブ割り当てが無いときだけ空とみなす
            return m_allocations.empty();
        }

        [[nodiscard]] size_t capacity() const noexcept
        {
            return m_capacity;
        }

        [[nodiscard]] size_t size() const noexcept
        {
            return m_used;
        }

        [[nodiscard]] size_t free_size() const noexcept
        {
            return m_capacity - m_used;
        }

        [[nodiscard]] size_t head_offset() const noexcept
        {
            // - 空のときは tail と同じ位置を head とみなし、分岐を増やさない
            if (m_allocations.empty())
            {
                return m_tail;
            }

            return m_allocations.front().offset;
        }

        [[nodiscard]] size_t tail_offset() const noexcept
        {
            return m_tail;
        }
    private:
        [[nodiscard]] static size_t align_up(size_t a_value, size_t a_alignment) noexcept
        {
            // - 0 除算を避けつつ、要求アラインメント単位へ切り上げる
            if (a_alignment <= 1)
            {
                return a_value;
            }

            const size_t remainder = a_value % a_alignment;
            return remainder == 0 ? a_value : (a_value + (a_alignment - remainder));
        }

        [[nodiscard]] bool commit_allocation(
            size_t a_offset,
            size_t a_size,
            size_t a_reservedSize,
            Allocation& a_outAllocation) noexcept
        {
            // - 予約情報を FIFO へ積み、解放時に wrap 分も含めて正しく戻せるようにする
            const Allocation allocation
            {
                .offset = a_offset,
                .size = a_size,
                .reservedSize = a_reservedSize,
                .id = m_nextAllocationId++
            };
            m_allocations.push_back(allocation);

            // - 消費済みサイズと tail を更新して、次の割り当て開始位置を確定する
            m_used += a_reservedSize;
            m_tail = (a_offset + a_size) % m_capacity;
            a_outAllocation = allocation;
            return true;
        }
    private:
        size_t m_capacity = 0;
        size_t m_tail = 0;
        size_t m_used = 0;
        uint64_t m_nextAllocationId = 1;
        std::deque<Allocation> m_allocations{};
    };
}
