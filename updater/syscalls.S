/* Native EX-word OS syscall dispatcher, if the engineering environment
   retains it. No write/erase/flash syscalls are linked into this image.
   WARNING: Availability at 0x74000000 is unverified on E models. */
.text
.balign 4
.global _exword_sys_findfirst
_exword_sys_findfirst:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x3f

.global _exword_sys_findnext
_exword_sys_findnext:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x40

.global _exword_sys_findclose
_exword_sys_findclose:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x41

.global _exword_sys_open
_exword_sys_open:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x37

.global _exword_sys_close
_exword_sys_close:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x38

.global _exword_sys_get_filesize
_exword_sys_get_filesize:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x3a

.global _exword_sys_seek
_exword_sys_seek:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x3b

.global _exword_sys_read
_exword_sys_read:
    mov.l .dispatch,r2
    mov.l 1f,r0
    jmp @r2
    nop
.balign 4
1:  .long 0x3c

.balign 4
.dispatch:
    .long 0x74000000
