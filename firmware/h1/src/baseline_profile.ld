/* SPDX-License-Identifier: Apache-2.0 */
/* This snippet is included inside SECTIONS. Give diagnostics a separate
 * allocation cursor over the frozen free SRAM1 interval, then reopen SECTIONS.
 * The MEMORY alias changes neither physical addressing nor cache attributes.
 */
}
MEMORY
{
    H1_BASELINE_DIAGNOSTIC (rw) : ORIGIN = 0x02500000, LENGTH = 65536
}
SECTIONS
{
.h1_baseline 0x02500000 (NOLOAD) :
{
    __h1_baseline_start = .;
    KEEP(*(.h1_baseline))
    __h1_baseline_end = .;
} > H1_BASELINE_DIAGNOSTIC
ASSERT(ADDR(.h1_baseline) == 0x02500000, "baseline diagnostic RAM moved")
ASSERT(SIZEOF(.h1_baseline) <= 65536, "baseline diagnostic RAM budget")
ASSERT(__SRAM1_end <= __h1_baseline_start, "baseline overlaps production SRAM1")
ASSERT(__h1_baseline_end <= 0x02510000, "baseline diagnostic RAM end")
ASSERT(__SRAM1_start == 0x02400000, "production SRAM1 base changed")
ASSERT(SIZEOF(SRAM1) == 545568, "production SRAM1 size changed")
ASSERT(__SRAM1_end == 0x02485320, "production SRAM1 end changed")
ASSERT(ALIGNOF(.h1_baseline) == 32, "baseline diagnostic RAM alignment")
ASSERT(h1BaselineState == __h1_baseline_start, "unexpected baseline diagnostic object")
ASSERT(ORIGIN(H1_BASELINE_DIAGNOSTIC) >= ORIGIN(SRAM1),
       "baseline diagnostic RAM below physical SRAM1")
ASSERT(ORIGIN(H1_BASELINE_DIAGNOSTIC) + LENGTH(H1_BASELINE_DIAGNOSTIC) <=
       ORIGIN(SRAM1) + LENGTH(SRAM1), "baseline diagnostic RAM above physical SRAM1")
