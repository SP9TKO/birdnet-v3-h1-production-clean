/* SPDX-License-Identifier: Apache-2.0 */
/* Separate diagnostic allocation; preserve all existing storage. */
}
MEMORY
{
    H1_VALIDATION_DIAGNOSTIC (rw) : ORIGIN = 0x02511000, LENGTH = 8192
}
SECTIONS
{
.h1_validation_observer 0x02511000 (NOLOAD) :
{
    __h1_validation_observer_start = .;
    KEEP(*(.h1_validation_observer))
    __h1_validation_observer_end = .;
} > H1_VALIDATION_DIAGNOSTIC
ASSERT(ADDR(.h1_validation_observer) == 0x02511000, "validation observer moved")
ASSERT(SIZEOF(.h1_validation_observer) == 7240, "validation observer storage ABI")
ASSERT(__h1_postprocess_observer_end <= __h1_validation_observer_start,
       "validation observer overlaps postprocess diagnostics")
ASSERT(__h1_validation_observer_end <= 0x02513000, "validation observer end")
ASSERT(h1ValidationObserverState == __h1_validation_observer_start,
       "unexpected validation observer object")
ASSERT(ALIGNOF(.h1_validation_observer) == 32, "validation observer alignment")
ASSERT(ORIGIN(H1_VALIDATION_DIAGNOSTIC) >= ORIGIN(SRAM1),
       "validation observer below physical SRAM1")
ASSERT(ORIGIN(H1_VALIDATION_DIAGNOSTIC) + LENGTH(H1_VALIDATION_DIAGNOSTIC) <=
       ORIGIN(SRAM1) + LENGTH(SRAM1), "validation observer above physical SRAM1")
