
/* Standard definitions of Mode bits and Interrupt (I & F) flags in PSRs */

	.equ    Mode_USR,       0x10
	.equ    Mode_FIQ,       0x11
	.equ    Mode_IRQ,       0x12
	.equ    Mode_SVC,       0x13
	.equ    Mode_ABT,       0x17
	.equ    Mode_UND,       0x1B
	.equ    Mode_SYS,       0x1F				/* available on ARM Arch 4 and later */

	.equ    I_Bit,          0x80				/* when I bit is set, IRQ is disabled */
	.equ    F_Bit,          0x40				/* when F bit is set, FIQ is disabled */

#ifndef PROGRAM_VERSION
#define PROGRAM_VERSION "0.1"
#endif
	.section .program_version
	    .ascii PROGRAM_VERSION

/* Startup Code must be linked first at Address at which it expects to run. */

	.text
	.arm
	.section .startup, "ax"

	.global _start

remap:

	 MOV R0, #0
	 LDR R1, = remap
	 LDR R2, = _end
copy_ram:
	 LDMIA R0!, {R3-R10}
	 STMIA R1!, {R3-R10}
	 CMP R1, R2
	 BLT copy_ram

_start:
	LDR     pc, =NextInst
NextInst:

/* Enter Supervisor Mode and set its Stack Pointer */
        MOV     R0, #Mode_SVC|I_Bit|F_Bit
        MSR     cpsr_c, R0
	LDR     sp, =__stack_end__

/* Relocate .data section (Copy from ROM to RAM) */
	LDR     r1, =_etext
	LDR     r2, =__data_start
	LDR     r3, =_edata
LoopRel:
	CMP     r2, r3
	LDRLO   r0, [r1], #4
	STRLO   r0, [r2], #4
	BLO     LoopRel

/* Clear .bss section (Zero init) */

	MOV     r0, #0
	LDR     r1, =__bss_start__
	LDR     r2, =__bss_end__
LoopZI:
	CMP     r1, r2
	STRLO   r0, [r1], #4
	BLO     LoopZI

/* Enter the C code, use B instruction so as to never return */
/* use BL main if you want to use c++ destructors below */
	B		main
/*
	LDR r0, =20000000
	BX  r0
*/
	.end
