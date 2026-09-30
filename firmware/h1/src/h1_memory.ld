.h1_fast_memory 0x02356B00 (NOLOAD) :
{
    __h1_fast_start = .;
    KEEP(*(.h1_fast_memory))
    __h1_fast_end = .;
} > SRAM0

ASSERT(ADDR(.alif_sram0) == 0x02000000, "H1 BASEP1 moved")
ASSERT(SIZEOF(.alif_sram0) == 3500800, "H1 BASEP1 reservation changed")
ASSERT(ADDR(.alif_sram0) + SIZEOF(.alif_sram0) == 0x02356B00,
       "BASEP1 overlaps BASEP2")
ASSERT(ADDR(.h1_fast_memory) == 0x02356B00, "H1 BASEP2 moved")
ASSERT(SIZEOF(.h1_fast_memory) == 524288, "H1 BASEP2 reservation changed")
ASSERT(__h1_fast_end == 0x023D6B00, "H1 BASEP2 end changed")
ASSERT(h1FastMemory == __h1_fast_start, "unexpected H1 BASEP2 object")
ASSERT(SIZEOF(SRAM0) == 0, "another section entered the H1 SRAM0 allocation")
