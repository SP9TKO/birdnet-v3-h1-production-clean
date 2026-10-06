/* SPDX-License-Identifier: Apache-2.0 */
.h1_baseline 0x02500000 (NOLOAD) :
{
    __h1_baseline_start = .;
    KEEP(*(.h1_baseline))
    __h1_baseline_end = .;
} > SRAM1
ASSERT(ADDR(.h1_baseline) == 0x02500000, "baseline diagnostic RAM moved")
ASSERT(SIZEOF(.h1_baseline) <= 65536, "baseline diagnostic RAM budget")
ASSERT(__SRAM1_end <= __h1_baseline_start, "baseline overlaps production SRAM1")
ASSERT(__h1_baseline_end <= 0x02510000, "baseline diagnostic RAM end")
