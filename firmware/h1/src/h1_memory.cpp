#include "h1_memory.hpp"

#include "h1_memory_contract.h"

#include <cmsis_core.h>
#include <zephyr/kernel.h>

extern "C" {
__attribute__((section(".alif_sram0.h1_arena"), aligned(32)))
uint8_t h1TensorArena[H1_ARENA_RESERVATION_BYTES];

__attribute__((section(".h1_fast_memory"), aligned(32)))
uint8_t h1FastMemory[H1_FAST_RESERVATION_BYTES];
}

namespace {
constexpr unsigned guardBytes = 64;

uint8_t pattern(unsigned index)
{
	return static_cast<uint8_t>(0xa5u ^ (index * 37u));
}
}

bool h1FastGuardPrepare()
{
	static_assert(H1_FAST_GUARD_OFFSET % 32 == 0);
	static_assert(H1_FAST_GUARD_OFFSET >= H1_FAST_REQUIRED_BYTES);
	static_assert(H1_FAST_GUARD_OFFSET + guardBytes <= H1_FAST_RESERVATION_BYTES);
	if (reinterpret_cast<uintptr_t>(h1TensorArena) != H1_ARENA_ADDRESS ||
	    reinterpret_cast<uintptr_t>(h1FastMemory) != H1_FAST_ADDRESS) {
		printk("H1_FAIL_MEMORY_ADDRESS arena=%p fast=%p\n", h1TensorArena, h1FastMemory);
		return false;
	}
	for (unsigned index = 0; index < guardBytes; ++index) {
		h1FastMemory[H1_FAST_GUARD_OFFSET + index] = pattern(index);
	}
	SCB_CleanInvalidateDCache_by_Addr(
		reinterpret_cast<uint32_t *>(h1FastMemory + H1_FAST_GUARD_OFFSET), guardBytes);
	printk("H1_FAST_GUARD_INIT address=%p bytes=%u required=%u reservation=%u\n",
	       h1FastMemory + H1_FAST_GUARD_OFFSET, guardBytes,
	       unsigned(H1_FAST_REQUIRED_BYTES), unsigned(H1_FAST_RESERVATION_BYTES));
	return true;
}

bool h1FastGuardCheck(unsigned run)
{
	constexpr unsigned guardBytes = 64;
	SCB_InvalidateDCache_by_Addr(
		reinterpret_cast<uint32_t *>(h1FastMemory + H1_FAST_GUARD_OFFSET), guardBytes);
	for (unsigned index = 0; index < guardBytes; ++index) {
		if (h1FastMemory[H1_FAST_GUARD_OFFSET + index] != pattern(index)) {
			printk("H1_FAIL_FAST_GUARD run=%u offset=%u expected=%02x actual=%02x\n",
			       run, unsigned(H1_FAST_GUARD_OFFSET + index), pattern(index),
			       h1FastMemory[H1_FAST_GUARD_OFFSET + index]);
			return false;
		}
	}
	printk("H1_FAST_GUARD_PASS run=%u\n", run);
	return true;
}
