/* Private Corstone map: addresses from the exact retained CMSIS pack. */
ENTRY(Reset_Handler)
MEMORY {
 BOOT (rx) : ORIGIN = 0x11000000, LENGTH = 128K
 DATA (rw) : ORIGIN = 0x70000000, LENGTH = 64K
 MODELS (r) : ORIGIN = 0x90000000, LENGTH = 256M
 INPUTS (rw) : ORIGIN = 0x71000000, LENGTH = 1M
}
SECTIONS {
 .vectors : { KEEP(*(.vectors)) } > BOOT
 .text : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) } > BOOT
 .ARM.extab : { *(.ARM.extab*) } > BOOT
 .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*) __exidx_end = .; } > BOOT
 .data : { *(.data*) } > DATA
 .bss (NOLOAD) : { . = ALIGN(8); __bss_start = .; *(.bss*) *(COMMON) . = ALIGN(8); __bss_end = .; } > DATA
 __heap_start = ALIGN(.,8); __heap_end = 0x70010000;
 __stack_top = 0x31210000;
 .sealed_backbone 0x90000000 : { KEEP(*(.sealed_backbone)) } > MODELS
 .sealed_classifier 0x92000010 : { KEEP(*(.sealed_classifier)) } > MODELS
 .fixture_backbone 0x71000000 : { KEEP(*(.fixture_backbone)) } > INPUTS
 .fixture_classifier 0x71080000 : { KEEP(*(.fixture_classifier)) } > INPUTS
 /DISCARD/ : { *(.comment) *(.note*) }
}
ASSERT(SIZEOF(.vectors)+SIZEOF(.text)+SIZEOF(.ARM.exidx)+SIZEOF(.ARM.extab)<=128K,"Boot ROM capacity exceeded")
ASSERT(__heap_start<__heap_end,"Runtime heap has no space")
