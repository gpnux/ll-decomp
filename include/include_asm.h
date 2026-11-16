#ifndef INCLUDE_ASM_H
#define INCLUDE_ASM_H

/* Generated ASM uses unified syntax; agbcc emits legacy divided Thumb syntax. */
#define INCLUDE_ASM(FOLDER, NAME) \
    __asm__ (".syntax unified\n.include \"" FOLDER "/" #NAME ".s\"\n.syntax divided\n")

#endif
