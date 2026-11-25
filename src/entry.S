.syntax unified
.arm
.section .text.start,"ax"
.global _start
_start:
    b init_module
    .word mod_header - _start

mod_header:
    .ascii "MOD0"
    .word dynamic_table - mod_header
    .word bss_start - mod_header
    .word bss_end - mod_header
    .word 0, 0
    .word module_object - mod_header

// DT_INIT is called by the guest dynamic linker. The guard also permits
// loaders that invoke the module's entry before its initializer.
init_module:
    ldr r2, guard_offset
guard_pc:
    add r2, pc, r2
    ldr r3, [r2]
    cmp r3, #0
    bxne lr
    mov r3, #1
    str r3, [r2]
    adr r0, _start
    b sharpness_log_init
guard_offset:
    .word initialized - (guard_pc + 8)

.section .rodata,"a"
    .word 0, name_end - name
name:
    .ascii "MHGU-Sharpness-Gauge"
name_end:
    .byte 0
    .balign 4
hash_table:
    .word 1, 1, 0, 0
symbols:
    .space 16
strings:
    .byte 0
.section .data,"aw"
.global dynamic_table
dynamic_table:
    .word 4, hash_table
    .word 5, strings
    .word 6, symbols
    .word 10, 1
    .word 11, 16
    .word 12, init_module
    .word 0, 0
.section .bss,"aw",%nobits
    .balign 16
module_object:
    .space 0x200
initialized:
    .space 4

// Every call below replaces a non-flag-setting instruction. The next retail
// instruction consumes its result. LR is already saved by the caller.
.macro landing kind
.section .landing.\kind,"ax"
.global \kind\()_capture, \kind\()_worker, \kind\()_present
\kind\()_capture: b handle_capture
\kind\()_worker:  b handle_worker
\kind\()_present: b handle_present
.endm
landing paired
landing solo

.text
// Preserve volatile integer registers and APSR. All three sites have integer
// signatures and no volatile VFP value live across the inserted call.
.macro enter_hook return_offset
    push {r0-r3,r12,lr}
    mrs r2, APSR
    push {r2,r3}
    movw r0, #((\return_offset) & 65535)
    movt r0, #((\return_offset) >> 16)
    sub r0, lr, r0
.endm

.macro leave_hook
    pop {r2,r3}
    msr APSR_nzcvq, r2
    pop {r0-r3,r12,lr}
.endm

.global handle_capture, handle_worker, handle_present
handle_capture:
    enter_hook 0xbbe678
    bl sharpness_sample
    leave_hook
    mov r4, r0
    bx lr

.macro present_hook name, return_offset
handle_\name:
    enter_hook \return_offset
    ldr r1, [sp,#8]  // original r0 = window offset 0x34100
    add r1, r5, r1
    ldr r1, [r1,#376]
    bl sharpness_present
    leave_hook
    add r0, r5, r0
    bx lr
.endm
present_hook worker, 0xbbd930
present_hook present, 0xbbe77c
