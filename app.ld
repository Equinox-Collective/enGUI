OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
    /* Базовый адрес приложения в Ring 3 */
    . = 0x1000000;

    /* Код */
    .text : {
        *(.text .text.*)
    }

    /* Константы и Таблицы виртуальных функций C++ */
    .rodata : {
        *(.rodata .rodata.*)
    }

    /* Инициализаторы конструкторов C++ */
    .data : {
        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        
        *(.data .data.*)
    }

    /* Неинициализированные данные */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}