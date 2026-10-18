/* Linker script of the can-mcu bootloader (src/lib/b1s/FIRMWARE.md): the
 * first 16 KB of flash. RAM is shared with the application, which starts
 * afresh. */
ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 16K
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 48K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)) } >FLASH
  .text : { . = ALIGN(4); *(.text*) *(.rodata*) . = ALIGN(4); } >FLASH
  .ARM.exidx : { *(.ARM.exidx*) } >FLASH
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } >RAM AT> FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) . = ALIGN(4); _ebss = .; } >RAM
  /DISCARD/ : { *(.init) *(.fini) *(.init_array*) *(.fini_array*) *(.ARM.attributes) }
}
