/* -*- asm -*- */

#include "globalconfig.h"
#include "config_tcbsize.h"
#include "tcboffset.h"
#include "asm_entry.h"

.arch_extension virt

.macro  switch_to_hyp_kernel
	sub	sp, sp, #RF_SIZE
	clrex
	str	lr, [sp, #RF(USR_LR, 0)]
	mrs	lr, ELR_hyp
	str	lr, [sp, #RF(PC, 0)]
	mrs	lr, spsr
	str	lr, [sp, #RF(PSR, 0)]
	mrs	lr, SP_usr
	str	lr, [sp, #RF(USR_SP, 0)]
.endm

.macro 	return_from_exception
	ldr	lr, [sp, #RF(PSR,0)]		@ Unstack SPSR
	add	sp, sp, #RF_SIZE		@ SP to top of stack
	msr	spsr_cfsx, lr			@ Load SPSR from kernel_lr
	ldr	lr, [sp, #RF(PC, -RF_SIZE)]
	msr	ELR_hyp, lr
	ldr	lr, [sp, #RF(USR_SP, -RF_SIZE)]
	msr	SP_usr, lr
	ldr	lr, [sp, #RF(USR_LR, -RF_SIZE)]
	eret
.endm

GEN_SYSCALL_TABLE

.macro disable_irqs
	cpsid	iaf
.endm

.align  4
.LCslowtrap_entry:	.word	slowtrap_entry

GEN_EXCEPTION_RETURN
GEN_IRET
GEN_VCPU_UPCALL OFS__THREAD__KERN_VCPU
GEN_LEAVE_BY_TRIGGER_EXCEPTION
GEN_LEAVE_AND_KILL_MYSELF
GEN_DEBUGGER_ENTRIES


.local hyp_irq_entry
hyp_irq_entry:
	switch_to_hyp_kernel

	stmdb	sp!, {r0 - r3, r12}   	@ Stack rest of user state
	align_and_save_sp r0
	mov	lr, pc
	ldr	pc, 1f
	ldr	sp, [sp]
	ldmia	sp, {r0 - r3, r12}		@ Restore user state
	disable_irqs
	add	sp, sp, #20
	return_from_exception

	.global __irq_handler_irq
__irq_handler_irq:
1:	.word	irq_handler


.p2align 5
.globl hyp_vector_base
hyp_vector_base:
	nop				/* Not used	*/
	b	hyp_undef_entry		/* UNDEF	*/
	b	hyp_swi_entry		/* SWI		*/
	b	hyp_inst_abort_entry	/* IABORT	*/
	b	hyp_data_abort_entry	/* DABORT	*/
	b	hyp_trap_entry		/* HYP TRAP	*/
	b	hyp_irq_entry		/* IRQ		*/
	b	hyp_irq_entry		/* FIQ		*/

.macro HYP_FAULT err
	switch_to_hyp_kernel
	stmdb	sp!, {r0 - r12}		@ Stack rest of user state
	sub	sp, #(4 * 2)		@ dummy PFA and dummy error code
	mov	r0, #\err
	b	hyp_fault_call
.endm

.align 4
hyp_undef_entry:
	HYP_FAULT 0

hyp_swi_entry:
	HYP_FAULT 1

hyp_inst_abort_entry:
	HYP_FAULT 2

hyp_data_abort_entry:
	HYP_FAULT 3

hyp_fault_call:
	mov	r1, sp
	align_and_save_sp r2
	bl	hyp_mode_fault
	ldr	sp, [sp]
	add	sp, sp, #8
	ldmia	sp, {r0 - r12}
	disable_irqs
	add	sp, sp, #(13 * 4)
	return_from_exception

hyp_trap_entry:
	switch_to_hyp_kernel
	stmdb	sp!, {r0 - r12}   	@ Stack rest of user state
	add	r0, sp, #13*4		@ dummy PFA and dummy error code
	sub	sp, sp, #8
	mov	lr, pc
	ldr	pc, 1f
	add	sp, sp, #8
.global fast_ret_from_irq
.type fast_ret_from_irq, #function
fast_ret_from_irq:
	ldmia	sp, {r0 - r12}		@ Restore user state
	disable_irqs
	add	sp, sp, #13*4
	return_from_exception
1:	.word arm_esr_entry

.global	vcpu_resume
.type vcpu_resume, #function
vcpu_resume:
	add	sp, r1, #RF_SIZE
	add	r0, r0, #8			@ pfa, err
	ldr	r1, [r0, #RF(PSR, 13*4)]	@ Unstack SPSR
	msr	spsr_cfsx, r1
	ldr	lr, [r0, #RF(USR_LR, 13*4)]	@ load LR_usr from vcpu state

	ldr	r1, [r0, #RF(USR_SP, 13*4)]
	msr	sp_usr, r1

	ldr	r1, [r0, #RF(PC, 13*4)]
	msr	ELR_hyp, r1
	ldmia	r0, {r0-r12}
	eret
