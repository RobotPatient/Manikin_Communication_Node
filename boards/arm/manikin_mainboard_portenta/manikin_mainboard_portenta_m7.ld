/* Linker script for STM32H747XI SoC (Cortex-M7 core) */

MEMORY
{
    FLASH (rx) : ORIGIN = 0x08000000, LENGTH = 2048K
    DTCM  (rwx): ORIGIN = 0x20000000, LENGTH = 128K
    RAM   (rwx): ORIGIN = 0x24000000, LENGTH = 512K
    AXISRAM (rwx): ORIGIN = 0x24000000, LENGTH = 512K
    SRAM1 (rwx): ORIGIN = 0x30000000, LENGTH = 128K
    SRAM2 (rwx): ORIGIN = 0x30020000, LENGTH = 128K
    SRAM3 (rwx): ORIGIN = 0x30040000, LENGTH = 32K
    SRAM4 (rwx): ORIGIN = 0x38000000, LENGTH = 64K
}

/* Include the common linker script */
#include <zephyr/arch/arm/cortex_m/scripts/linker.ld>