/* boards/stm32f407g-disc1/stm32f407.ld
 * STM32F407VG: 1 MB flash @ 0x08000000, 128 KB SRAM @ 0x20000000
 * (64 KB CCM at 0x10000000 is intentionally not used: no DMA access.)
 */
ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 1024K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);   /* end of SRAM: initial MSP */

SECTIONS
{
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } > FLASH

    .text :
    {
        . = ALIGN(4);
        *(.text .text.*)                 /* program code */
        *(.rodata .rodata.*)             /* read-only data */
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data :
    {
        . = ALIGN(4);
        _sdata = .;
        *(.data .data.*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT> FLASH

    /* Must follow the .data section: LOADADDR resolves its flash load image. */
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) :
    {
        . = ALIGN(8);
        _sbss = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(8);
        _ebss = .;
    } > SRAM

    .noinit (NOLOAD) :
    {
        *(.noinit .noinit.*)
    } > SRAM

    /DISCARD/ :
    {
        libc.a ( * )
        libm.a ( * )
    }

    .ARM.attributes 0 : { *(.ARM.attributes) }
}
