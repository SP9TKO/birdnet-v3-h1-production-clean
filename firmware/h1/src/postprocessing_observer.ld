/* SPDX-License-Identifier: Apache-2.0 */
/* Separate diagnostic allocation cursor; no production storage changes. */
}
MEMORY
{
    H1_POSTPROCESS_DIAGNOSTIC (rw) : ORIGIN = 0x02510000, LENGTH = 4096
}
SECTIONS
{
.h1_postprocess_observer 0x02510000 (NOLOAD) :
{
    __h1_postprocess_observer_start = .;
    KEEP(*(.h1_postprocess_observer))
    __h1_postprocess_observer_end = .;
} > H1_POSTPROCESS_DIAGNOSTIC
ASSERT(ADDR(.h1_postprocess_observer) == 0x02510000, "postprocess observer moved")
ASSERT(SIZEOF(.h1_postprocess_observer) <= 4096, "postprocess observer budget")
ASSERT(__h1_baseline_end <= __h1_postprocess_observer_start,
       "postprocess observer overlaps baseline diagnostics")
ASSERT(__h1_postprocess_observer_end <= 0x02511000, "postprocess observer end")
ASSERT(h1PostprocessObserverState == __h1_postprocess_observer_start,
       "unexpected postprocess observer object")
ASSERT(ALIGNOF(.h1_postprocess_observer) == 32, "postprocess observer alignment")
ASSERT(ORIGIN(H1_POSTPROCESS_DIAGNOSTIC) >= ORIGIN(SRAM1),
       "postprocess observer below physical SRAM1")
ASSERT(ORIGIN(H1_POSTPROCESS_DIAGNOSTIC) + LENGTH(H1_POSTPROCESS_DIAGNOSTIC) <=
       ORIGIN(SRAM1) + LENGTH(SRAM1), "postprocess observer above physical SRAM1")
