.section .header,"a"
.ascii "CASIO CY10x ADDIN MODULE FOR RELEASE ROM\0"
.balign 16,0
.long 0x8c18ff80
.long _start
.space 72

.section .text.start,"ax"
.global _start
_start:
    /* Physical P1 entry. A dedicated stack is in the bootstrap page
       and remains outside the embedded full app's binary mapping. */
    stc sr,r0
    mov #0xf0,r1
    extu.b r1,r1
    or r1,r0
    ldc r0,sr
    mov.l .stack,r15
    mov.l .main,r0
    jsr @r0
    nop
.halt:
    bra .halt
    nop
.balign 4
.stack: .long 0x8c190ff0
.main:  .long _updater_full_boot
