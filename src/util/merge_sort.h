// merge_sort.h: a stable merge sort used by sort(array, comparator) in BOTH engines.
// std::sort is not safe here as it assumes the ordering function is a strict weak ordering, and a user-written comparator can be anything (always returns 1, depends on a counter, ...). With such a comparator std::sort can read outside the array. A merge sort only ever moves elements between two buffers of the right size, so a nonsensical comparator gives a nonsensical ORDER but never memory corruption. It is also stable (equal elements keep their order), which is what people expect from a comparator-based sort, and both engines use this one function so they always produce the same order.
#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

// Sorts `items` in place. `greater(a, b)` must return true when `a` has to come AFTER `b`. It is called with `a` from the left run being merged and `b` from the right run, and an element is taken from the right run only when greater(left, right) is true, which is what keeps equal elements in their original order.
// `greater` may throw (a comparator written in the language can raise an error): `items` is then left in an unspecified order, so callers sort a COPY and keep the original unless this returns normally.
template <class T, class Greater>
void stableMergeSort(std::vector<T>& items, Greater greater) {
    const std::size_t n = items.size();
    if (n < 2) return;
    std::vector<T> buffer(n);
    std::vector<T>* from = &items;
    std::vector<T>* to = &buffer;
    for (std::size_t width = 1; width < n; width *= 2) {
        for (std::size_t lo = 0; lo < n; lo += 2 * width) {
            const std::size_t mid = std::min(lo + width, n);
            const std::size_t hi = std::min(lo + 2 * width, n);
            std::size_t left = lo, right = mid, out = lo;
            while (left < mid && right < hi) {
                if (greater((*from)[left], (*from)[right])) (*to)[out++] = std::move((*from)[right++]);
                else (*to)[out++] = std::move((*from)[left++]);
            }
            while (left < mid) (*to)[out++] = std::move((*from)[left++]);
            while (right < hi) (*to)[out++] = std::move((*from)[right++]);
        }
        std::swap(from, to); // the merged runs are now in `from`
    }
    if (from != &items) items = std::move(*from);
}