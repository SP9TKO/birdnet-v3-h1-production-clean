/* SPDX-License-Identifier: Apache-2.0 */
/* Bounded lifecycle metadata; independent cursor preserves production storage. */
}
MEMORY
{
    H1_WAVEFORM_METADATA (rw) : ORIGIN = 0x02513000, LENGTH = 4096
}
SECTIONS
{
.h1_waveform_lifecycle 0x02513000 (NOLOAD) :
{
    __h1_waveform_lifecycle_start = .;
    KEEP(*(.h1_waveform_lifecycle))
    __h1_waveform_lifecycle_end = .;
} > H1_WAVEFORM_METADATA
ASSERT(ADDR(.h1_waveform_lifecycle) == 0x02513000, "waveform metadata moved")
ASSERT(SIZEOF(.h1_waveform_lifecycle) <= 4096, "waveform metadata budget")
ASSERT(__h1_waveform_lifecycle_end <= 0x02514000, "waveform metadata end")
ASSERT(h1WaveformLifecycle == __h1_waveform_lifecycle_start,
       "unexpected waveform metadata object")
ASSERT(ALIGNOF(.h1_waveform_lifecycle) == 32, "waveform metadata alignment")
ASSERT(ORIGIN(H1_WAVEFORM_METADATA) >= ORIGIN(SRAM1), "waveform metadata below SRAM1")
ASSERT(ORIGIN(H1_WAVEFORM_METADATA) + LENGTH(H1_WAVEFORM_METADATA) <=
       ORIGIN(SRAM1) + LENGTH(SRAM1), "waveform metadata above SRAM1")
ASSERT(__SRAM1_end <= __h1_waveform_lifecycle_start,
       "waveform metadata overlaps production")
