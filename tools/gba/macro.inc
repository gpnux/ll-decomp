/* gbadisasm function markers. Alignment bytes are emitted explicitly by gba_split. */
.ifndef GBA_SPLIT_MACROS
.set GBA_SPLIT_MACROS, 1
.syntax unified
.arch armv4t

.macro arm_func_start name
    .arm
    .global \name
    .type \name, %function
.endm

.macro thumb_func_start name
    .thumb
    .global \name
    .type \name, %function
    .thumb_func
.endm

.macro non_word_aligned_thumb_func_start name
    thumb_func_start \name
.endm

.endif
