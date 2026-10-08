/* SPDX-License-Identifier: Apache-2.0 */
/* Independent bounded result/session/pin/proof metadata; no boundary snapshot. */
}
MEMORY
{
    H1_PRODUCTION_METADATA (rw) : ORIGIN = 0x02514000, LENGTH = 4096
}
SECTIONS
{
.h1_production_result 0x02514000 (NOLOAD) :
{
    __h1_production_result_start = .;
    KEEP(*(.h1_production_result))
    __h1_production_result_end = .;
} > H1_PRODUCTION_METADATA
ASSERT(ADDR(.h1_production_result) == 0x02514000, "production metadata moved")
ASSERT(SIZEOF(.h1_production_result) <= 4096, "production metadata budget")
ASSERT(h1ProductionRuntime == __h1_production_result_start, "unexpected result metadata object")
ASSERT(ALIGNOF(.h1_production_result) == 32, "production metadata alignment")
ASSERT(__h1_waveform_lifecycle_end <= __h1_production_result_start, "production metadata overlaps waveform")
ASSERT(ORIGIN(H1_PRODUCTION_METADATA) >= ORIGIN(SRAM1), "production metadata below SRAM1")
ASSERT(ORIGIN(H1_PRODUCTION_METADATA) + LENGTH(H1_PRODUCTION_METADATA) <=
       ORIGIN(SRAM1) + LENGTH(SRAM1), "production metadata above SRAM1")
ASSERT(__SRAM1_end <= __h1_production_result_start, "production metadata overlaps production")
