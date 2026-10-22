// tim_sort.h: Timsort, the sort used by sort() in BOTH engines (plain sort(arr) and sort(arr, comparator)).
// Follows CPython's listsort (Objects/listobject.c, listsort.txt) closely, including its care that NOTHING bad can happen if the ordering function is inconsistent (the comparator is user code and can be anything): all index arithmetic is bounded by the lengths involved, so a nonsensical comparator gives a nonsensical ORDER but never an out-of-range access, and the result is always a permutation of the input.
// Only `less(a, b)` is ever called ("is a strictly before b"). `less` may throw (a comparator written in the language can raise an error): `items` is then left in an unspecified order (some elements may have been moved out), so callers that can fail sort a COPY and keep the original unless this returns normally.
#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace timsort_detail {

constexpr std::ptrdiff_t MIN_GALLOP = 7; // how many times in a row one side must win before galloping starts (adapts as the sort runs)

template <class T, class Less>
class TimSorter {
public:
    TimSorter(std::vector<T>& items, Less& less)
        : data(items.data()), size(static_cast<std::ptrdiff_t>(items.size())), less(less) {}

    void run() {
        if (size < 2) return;
        std::ptrdiff_t lo = 0;
        std::ptrdiff_t remaining = size;
        const std::ptrdiff_t minRun = computeMinRun(size);
        while (remaining > 0) {
            std::ptrdiff_t runLength = countRun(lo, lo + remaining);
            if (runLength < minRun) {
                const std::ptrdiff_t forced = std::min(minRun, remaining);
                binaryInsertionSort(lo, lo + forced, lo + runLength);
                runLength = forced;
            }
            runs.push_back(Run{lo, runLength});
            mergeCollapse();
            lo += runLength;
            remaining -= runLength;
        }
        mergeForceCollapse();
    }

private:
    struct Run {
        std::ptrdiff_t base;
        std::ptrdiff_t length;
    };

    T* data;
    std::ptrdiff_t size;
    Less& less;
    std::vector<T> tmp;                      // holds the shorter of the two runs being merged
    std::ptrdiff_t minGallop = MIN_GALLOP;   // adaptive threshold
    std::vector<Run> runs;                   // the stack of pending runs

    // A length in [32, 64] such that n / minrun is a power of two or just under one, which makes the final merges balanced. Under 64 elements the whole array is one binary-insertion-sorted run.
    static std::ptrdiff_t computeMinRun(std::ptrdiff_t n) {
        std::ptrdiff_t r = 0; // becomes 1 if any 1 bits are shifted off
        while (n >= 64) {
            r |= n & 1;
            n >>= 1;
        }
        return n + r;
    }

    // Length of the run starting at `lo`: non-descending, or STRICTLY descending (then reversed in place, which keeps the sort stable because no two of its elements are equal).
    std::ptrdiff_t countRun(std::ptrdiff_t lo, std::ptrdiff_t hi) {
        std::ptrdiff_t i = lo + 1;
        if (i == hi) return 1;
        std::ptrdiff_t n = 2;
        if (less(data[i], data[lo])) {
            for (i = lo + 2; i < hi; ++i, ++n) {
                if (!less(data[i], data[i - 1])) break;
            }
            std::reverse(data + lo, data + lo + n);
        } else {
            for (i = lo + 2; i < hi; ++i, ++n) {
                if (less(data[i], data[i - 1])) break;
            }
        }
        return n;
    }

    // Sorts data[lo, hi) given that data[lo, start) is already sorted: insertion sort that finds each position by binary search (few comparisons; the moves are cheap for short runs). Stable: an element goes AFTER equal ones.
    void binaryInsertionSort(std::ptrdiff_t lo, std::ptrdiff_t hi, std::ptrdiff_t start) {
        if (lo == start) ++start;
        for (; start < hi; ++start) {
            T pivot = std::move(data[start]);
            std::ptrdiff_t left = lo;
            std::ptrdiff_t right = start;
            while (left < right) {
                const std::ptrdiff_t mid = left + ((right - left) >> 1);
                if (less(pivot, data[mid])) right = mid;
                else left = mid + 1;
            }
            for (std::ptrdiff_t p = start; p > left; --p) data[p] = std::move(data[p - 1]);
            data[left] = std::move(pivot);
        }
    }

    // Keeps the run lengths on the stack balanced (so merges stay about equal in size). The two conditions are the corrected invariants (de Gouw et al. 2015) that CPython and Java use today.
    void mergeCollapse() {
        while (runs.size() > 1) {
            std::size_t n = runs.size() - 2;
            if ((n > 0 && runs[n - 1].length <= runs[n].length + runs[n + 1].length) ||
                (n > 1 && runs[n - 2].length <= runs[n - 1].length + runs[n].length)) {
                if (runs[n - 1].length < runs[n + 1].length) --n;
                mergeAt(n);
            } else if (runs[n].length <= runs[n + 1].length) {
                mergeAt(n);
            } else {
                break;
            }
        }
    }

    // At the end: merge everything that is left.
    void mergeForceCollapse() {
        while (runs.size() > 1) {
            std::size_t n = runs.size() - 2;
            if (n > 0 && runs[n - 1].length < runs[n + 1].length) --n;
            mergeAt(n);
        }
    }

    // Merges runs i and i+1 (adjacent on the stack, adjacent in the array).
    void mergeAt(std::size_t i) {
        std::ptrdiff_t baseA = runs[i].base;
        std::ptrdiff_t lengthA = runs[i].length;
        const std::ptrdiff_t baseB = runs[i + 1].base;
        std::ptrdiff_t lengthB = runs[i + 1].length;
        runs[i].length = lengthA + lengthB;
        if (i + 3 == runs.size()) runs[i + 1] = runs[i + 2];
        runs.pop_back();

        // Where does B's first element go inside A? Everything in A before that is already in its final place.
        const std::ptrdiff_t skip = gallopRight(data[baseB], data + baseA, lengthA, 0);
        baseA += skip;
        lengthA -= skip;
        if (lengthA == 0) return;

        // Where does A's last element go inside B? Everything in B after that is already in its final place.
        lengthB = gallopLeft(data[baseA + lengthA - 1], data + baseB, lengthB, lengthB - 1);
        if (lengthB <= 0) return;

        // Copy the SHORTER run to the temporary buffer and merge towards it.
        if (lengthA <= lengthB) mergeLow(baseA, lengthA, baseB, lengthB);
        else mergeHigh(baseA, lengthA, baseB, lengthB);
    }

    // Returns k in [0, n] such that base[k-1] < key <= base[k] (the position just LEFT of any elements equal to key). `hint` in [0, n-1] says where to start looking: galloping outwards from it costs about log(distance) comparisons.
    std::ptrdiff_t gallopLeft(const T& key, const T* base, std::ptrdiff_t n, std::ptrdiff_t hint) {
        std::ptrdiff_t lastOfs = 0;
        std::ptrdiff_t ofs = 1;
        if (less(base[hint], key)) {
            // base[hint] < key: gallop right until base[hint + lastOfs] < key <= base[hint + ofs]
            const std::ptrdiff_t maxOfs = n - hint;
            while (ofs < maxOfs) {
                if (less(base[hint + ofs], key)) {
                    lastOfs = ofs;
                    ofs = (ofs << 1) + 1;
                    if (ofs <= 0) ofs = maxOfs; // overflow
                } else {
                    break;
                }
            }
            if (ofs > maxOfs) ofs = maxOfs;
            lastOfs += hint;
            ofs += hint;
        } else {
            // key <= base[hint]: gallop left until base[hint - ofs] < key <= base[hint - lastOfs]
            const std::ptrdiff_t maxOfs = hint + 1;
            while (ofs < maxOfs) {
                if (less(base[hint - ofs], key)) break;
                lastOfs = ofs;
                ofs = (ofs << 1) + 1;
                if (ofs <= 0) ofs = maxOfs;
            }
            if (ofs > maxOfs) ofs = maxOfs;
            const std::ptrdiff_t k = lastOfs;
            lastOfs = hint - ofs;
            ofs = hint - k;
        }
        // Now base[lastOfs] < key <= base[ofs]: binary search in between.
        ++lastOfs;
        while (lastOfs < ofs) {
            const std::ptrdiff_t m = lastOfs + ((ofs - lastOfs) >> 1);
            if (less(base[m], key)) lastOfs = m + 1;
            else ofs = m;
        }
        return ofs;
    }

    // Like gallopLeft, but returns k such that base[k-1] <= key < base[k] (just RIGHT of any elements equal to key).
    std::ptrdiff_t gallopRight(const T& key, const T* base, std::ptrdiff_t n, std::ptrdiff_t hint) {
        std::ptrdiff_t lastOfs = 0;
        std::ptrdiff_t ofs = 1;
        if (less(key, base[hint])) {
            // key < base[hint]: gallop left until base[hint - ofs] <= key < base[hint - lastOfs]
            const std::ptrdiff_t maxOfs = hint + 1;
            while (ofs < maxOfs) {
                if (less(key, base[hint - ofs])) {
                    lastOfs = ofs;
                    ofs = (ofs << 1) + 1;
                    if (ofs <= 0) ofs = maxOfs;
                } else {
                    break;
                }
            }
            if (ofs > maxOfs) ofs = maxOfs;
            const std::ptrdiff_t k = lastOfs;
            lastOfs = hint - ofs;
            ofs = hint - k;
        } else {
            // base[hint] <= key: gallop right until base[hint + lastOfs] <= key < base[hint + ofs]
            const std::ptrdiff_t maxOfs = n - hint;
            while (ofs < maxOfs) {
                if (less(key, base[hint + ofs])) break;
                lastOfs = ofs;
                ofs = (ofs << 1) + 1;
                if (ofs <= 0) ofs = maxOfs;
            }
            if (ofs > maxOfs) ofs = maxOfs;
            lastOfs += hint;
            ofs += hint;
        }
        ++lastOfs;
        while (lastOfs < ofs) {
            const std::ptrdiff_t m = lastOfs + ((ofs - lastOfs) >> 1);
            if (less(key, base[m])) ofs = m;
            else lastOfs = m + 1;
        }
        return ofs;
    }

    void ensureTemp(std::ptrdiff_t count) {
        if (static_cast<std::ptrdiff_t>(tmp.size()) < count) tmp.resize(static_cast<std::size_t>(count));
    }

    // Merges A = data[baseA, baseA+na) and B = data[baseB, baseB+nb), na <= nb, where baseB == baseA + na: A is copied out to the temporary buffer and the merge fills the array from the left.
    // Preconditions (from mergeAt, true for any consistent ordering): B[0] < A[0], and A's last element is greater than B's last.
    // The code never assumes the ordering is consistent: every count is checked before it is used, so a bad `less` can only misorder.
    void mergeLow(std::ptrdiff_t baseA, std::ptrdiff_t na, std::ptrdiff_t baseB, std::ptrdiff_t nb) {
        ensureTemp(na);
        T* const tmpA = tmp.data();
        std::move(data + baseA, data + baseA + na, tmpA);
        T* dest = data + baseA;
        T* pa = tmpA;
        T* pb = data + baseB;
        std::ptrdiff_t k, aCount, bCount, gallop; // declared up front: the gotos below must not jump over an initialization

        *dest++ = std::move(*pb++);
        --nb;
        if (nb == 0) goto succeed;
        if (na == 1) goto copyB;

        gallop = minGallop;
        for (;;) {
            aCount = 0;
            bCount = 0;
            // The straightforward merge, until one side has won `gallop` times in a row.
            for (;;) {
                if (less(*pb, *pa)) {
                    *dest++ = std::move(*pb++);
                    ++bCount;
                    aCount = 0;
                    --nb;
                    if (nb == 0) goto succeed;
                    if (bCount >= gallop) break;
                } else {
                    *dest++ = std::move(*pa++);
                    ++aCount;
                    bCount = 0;
                    --na;
                    if (na == 1) goto copyB;
                    if (aCount >= gallop) break;
                }
            }
            // One side is winning so consistently that galloping should pay off.
            ++gallop;
            do {
                gallop -= (gallop > 1);
                minGallop = gallop;
                k = gallopRight(*pb, pa, na, 0); // how many of A's elements belong before B's next element
                aCount = k;
                if (k) {
                    std::move(pa, pa + k, dest);
                    dest += k;
                    pa += k;
                    na -= k;
                    if (na == 1) goto copyB;
                    if (na == 0) goto succeed; // impossible for a consistent ordering, but not assumed
                }
                *dest++ = std::move(*pb++);
                --nb;
                if (nb == 0) goto succeed;

                k = gallopLeft(*pa, pb, nb, 0); // how many of B's elements belong before A's next element
                bCount = k;
                if (k) {
                    std::move(pb, pb + k, dest);
                    dest += k;
                    pb += k;
                    nb -= k;
                    if (nb == 0) goto succeed;
                }
                *dest++ = std::move(*pa++);
                --na;
                if (na == 1) goto copyB;
            } while (aCount >= MIN_GALLOP || bCount >= MIN_GALLOP);
            ++gallop; // penalty for leaving galloping mode
            minGallop = gallop;
        }
    succeed:
        if (na > 0) std::move(pa, pa + na, dest);
        return;
    copyB:
        // na == 1: A's last element belongs after everything that is left of B.
        std::move(pb, pb + nb, dest);
        dest[nb] = std::move(*pa);
    }

    // Mirror image of mergeLow for na > nb: B is copied out and the merge fills the array from the RIGHT.
    void mergeHigh(std::ptrdiff_t baseA, std::ptrdiff_t na, std::ptrdiff_t baseB, std::ptrdiff_t nb) {
        ensureTemp(nb);
        T* const tmpB = tmp.data();
        std::move(data + baseB, data + baseB + nb, tmpB);
        T* dest = data + baseB + nb - 1;
        T* pb = tmpB + nb - 1;
        T* pa = data + baseA + na - 1;
        T* const startA = data + baseA;
        T* const startB = tmpB;
        std::ptrdiff_t k, aCount, bCount, gallop;

        *dest-- = std::move(*pa--);
        --na;
        if (na == 0) goto succeed;
        if (nb == 1) goto copyA;

        gallop = minGallop;
        for (;;) {
            aCount = 0;
            bCount = 0;
            for (;;) {
                if (less(*pb, *pa)) {
                    *dest-- = std::move(*pa--);
                    ++aCount;
                    bCount = 0;
                    --na;
                    if (na == 0) goto succeed;
                    if (aCount >= gallop) break;
                } else {
                    *dest-- = std::move(*pb--);
                    ++bCount;
                    aCount = 0;
                    --nb;
                    if (nb == 1) goto copyA;
                    if (bCount >= gallop) break;
                }
            }
            ++gallop;
            do {
                gallop -= (gallop > 1);
                minGallop = gallop;
                k = gallopRight(*pb, startA, na, na - 1);
                k = na - k;
                aCount = k;
                if (k) {
                    dest -= k;
                    pa -= k;
                    std::move_backward(pa + 1, pa + 1 + k, dest + 1 + k);
                    na -= k;
                    if (na == 0) goto succeed;
                }
                *dest-- = std::move(*pb--);
                --nb;
                if (nb == 1) goto copyA;

                k = gallopLeft(*pa, startB, nb, nb - 1);
                k = nb - k;
                bCount = k;
                if (k) {
                    dest -= k;
                    pb -= k;
                    std::move(pb + 1, pb + 1 + k, dest + 1);
                    nb -= k;
                    if (nb == 1) goto copyA;
                    if (nb == 0) goto succeed; // impossible for a consistent ordering, but not assumed
                }
                *dest-- = std::move(*pa--);
                --na;
                if (na == 0) goto succeed;
            } while (aCount >= MIN_GALLOP || bCount >= MIN_GALLOP);
            ++gallop;
            minGallop = gallop;
        }
    succeed:
        if (nb > 0) std::move(startB, startB + nb, dest - (nb - 1));
        return;
    copyA:
        // nb == 1: B's first element belongs before everything that is left of A.
        dest -= na;
        pa -= na;
        std::move_backward(pa + 1, pa + 1 + na, dest + 1 + na);
        *dest = std::move(*pb);
    }
};

} // namespace timsort_detail

// Sorts `items` in place; stable. `less(a, b)` must say whether a belongs strictly before b.
template <class T, class Less>
void timSort(std::vector<T>& items, Less less) {
    timsort_detail::TimSorter<T, Less> sorter(items, less);
    sorter.run();
}