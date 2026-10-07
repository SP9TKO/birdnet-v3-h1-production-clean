// SPDX-FileCopyrightText: 2026 Taras Kuchynskyy
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

// Optional diagnostic counts. They never participate in selection decisions.
struct H1TopkHeapCounts {
	uint32_t classesVisited, rootComparisons, betterScoreCalls, rootReplacements;
	uint32_t siftUpComparisons, siftDownComparisons, heapSwaps, drainRemovals;
};

// Scores are read-only. The caller supplies the existing score/index comparator
// and the sole index buffer. Every parent is no better than either child.
template <bool Observe, typename Better>
size_t h1SelectTopkHeap(const float *scores, uint32_t classes, uint32_t *top,
		      size_t capacity, Better better, H1TopkHeapCounts &counts)
{
	if (capacity == 0) return 0;
	const auto compare = [&](uint32_t left, uint32_t right) {
		if (Observe) ++counts.betterScoreCalls;
		return better(scores, left, right);
	};
	const auto swap = [&](size_t left, size_t right) {
		const uint32_t saved = top[left];
		top[left] = top[right];
		top[right] = saved;
		if (Observe) ++counts.heapSwaps;
	};
	const auto siftDown = [&](size_t live) {
		size_t parent = 0;
		while (2 * parent + 1 < live) {
			size_t child = 2 * parent + 1;
			if (child + 1 < live) {
				if (Observe) ++counts.siftDownComparisons;
				if (compare(top[child], top[child + 1])) ++child;
			}
			if (Observe) ++counts.siftDownComparisons;
			if (!compare(top[parent], top[child])) break;
			swap(parent, child);
			parent = child;
		}
	};
	size_t count = 0;
	for (uint32_t index = 0; index < classes; ++index) {
		if (Observe) ++counts.classesVisited;
		if (count < capacity) {
			size_t child = count++;
			top[child] = index;
			while (child != 0) {
				const size_t parent = (child - 1) / 2;
				if (Observe) ++counts.siftUpComparisons;
				if (!compare(top[parent], top[child])) break;
				swap(parent, child);
				child = parent;
			}
		} else {
			if (Observe) ++counts.rootComparisons;
			if (!compare(index, top[0])) continue;
			if (Observe) ++counts.rootReplacements;
			top[0] = index;
			siftDown(count);
		}
	}
	// Removed worst entries fill the output suffix, beyond the live heap.
	const size_t selected = count;
	while (count != 0) {
		const uint32_t worst = top[0];
		--count;
		if (Observe) ++counts.drainRemovals;
		if (count != 0) {
			top[0] = top[count];
			siftDown(count);
		}
		top[count] = worst;
	}
	return selected;
}
