#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "common.hpp"

namespace astar
{
    // The only tie-breaking policies used by the experiments.
    //
    // NodeId   : (f, id)                  -- deterministic, no goal-directed tie-break
    // LargerG  : (f, -g, id)             -- among equal f, prefer the vertex closer
    //                                      to the source (larger g), a standard A*
    //                                      tie-break that tends to follow one path
    enum class TieBreak : std::uint8_t { NodeId = 0, LargerG = 1 };

    struct HeapEntry {
        Distance f = 0; // priority: g for Dijkstra, g + h for A*
        Distance g = 0; // best known distance from the source
        Vertex node = 0;

        bool operator==(const HeapEntry &other) const
        {
            return f == other.f && g == other.g && node == other.node;
        }
    };

    // A binary min-heap written for this project.  It stores duplicate entries;
    // the search layer recognises and discards stale entries (lazy deletion).
    //
    // Interface and costs (n = number of stored entries):
    //   push(entry)  O(log n) worst case
    //   pop()        O(log n) worst case
    //   top()        O(1)
    //   empty/size   O(1)
    //   clear()      O(1)
    class BinaryHeap {
    public:
        explicit BinaryHeap(TieBreak tie = TieBreak::LargerG, std::size_t reserve = 0)
            : tie_(tie)
        {
            if (reserve != 0) data_.reserve(reserve);
        }

        void reserve(std::size_t n) { data_.reserve(n); }

        void clear() noexcept { data_.clear(); }

        [[nodiscard]] bool empty() const noexcept { return data_.empty(); }

        [[nodiscard]] std::size_t size() const noexcept { return data_.size(); }

        [[nodiscard]] TieBreak tie_break() const noexcept { return tie_; }

        [[nodiscard]] const HeapEntry &top() const
        {
            assert(!empty());
            return data_.front();
        }

        void push(const HeapEntry &entry)
        {
            data_.push_back(entry);
            sift_up(data_.size() - 1);
        }

        HeapEntry pop()
        {
            assert(!empty());
            HeapEntry result = data_.front();
            data_.front() = data_.back();
            data_.pop_back();
            if (!data_.empty()) sift_down(0);
            return result;
        }

    private:
        // True if a has strictly higher priority than b.
        [[nodiscard]] bool less(const HeapEntry &a, const HeapEntry &b) const noexcept
        {
            if (a.f != b.f) return a.f < b.f;
            if (tie_ == TieBreak::LargerG && a.g != b.g) return a.g > b.g;
            if (a.node != b.node) return a.node < b.node;
            // Duplicate entries for the same vertex: order them deterministically.
            return a.g < b.g;
        }

        void sift_up(std::size_t i)
        {
            while (i > 0)
            {
                const std::size_t parent = (i - 1) / 2;
                if (!less(data_[i], data_[parent])) break;
                std::swap(data_[i], data_[parent]);
                i = parent;
            }
        }

        void sift_down(std::size_t i)
        {
            const std::size_t n = data_.size();
            for (;;)
            {
                std::size_t best = i;
                const std::size_t left = 2 * i + 1;
                const std::size_t right = 2 * i + 2;
                if (left < n && less(data_[left], data_[best])) best = left;
                if (right < n && less(data_[right], data_[best])) best = right;
                if (best == i) break;
                std::swap(data_[i], data_[best]);
                i = best;
            }
        }

        std::vector<HeapEntry> data_;
        TieBreak tie_;
    };
} // namespace astar
