/**
 * \file
 * \brief Dispatcher architecture-specific implementation.
 */

/*
 * Copyright (c) 2007-2010,2012, ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/dispatch.h>
#include <barrelfish/dispatcher_arch.h>
#include <barrelfish/syscalls.h>
#include <barrelfish/static_assert.h>
#include "threads_priv.h"
#include <stdio.h>//for debugging printf

#include <asmoffsets.h>
#ifndef OFFSETOF_DISP_DISABLED
#error "Pants!"
#endif

/* entry points defined in assembler code */
extern void run_entry(void);
extern void pagefault_entry(void);
extern void disabled_pagefault_entry(void);
extern void trap_entry(void);

void __attribute__ ((visibility ("hidden"))) disp_resume_context_epilog(void);
void __attribute__ ((visibility ("hidden"))) disp_switch_epilog(void);
void __attribute__ ((visibility ("hidden"))) disp_save_epilog(void);
void __attribute__ ((visibility ("hidden"))) disp_save_rm_kcb_epilog(void);

///////////////////////////////////////////////////////////////////////////////
//
// Low level "C" context switch related code
//

STATIC_ASSERT(SPSR_REG == 0,  "broken context assumption");
STATIC_ASSERT(NUM_REGS == 34, "broken context assumption");
STATIC_ASSERT(PC_REG   == 33, "broken context assumption");

/*
 * XXX: there is no guarantee that the context has been set up by
 * disp_save_context, so we can not cut corners by not restoring registers
 * clobbered in disp_save_context.
 * e.g. when a new thread is created, it is started using this function, with x0 and x1
 * being arguments.
 */
static void __attribute__((noinline, optimize(2)))
disp_resume_context(struct dispatcher_shared_generic *disp, uint64_t *regs)
{
    __asm volatile(
        /* Re-enable dispatcher */
        "    mov     w30, #0                                             \n\t"
        "    str     w30, [%[disp], # " XTR(OFFSETOF_DISP_DISABLED) "]        \n\t"
		/* Restore SP*/
		"    ldr     x30,  [%[regs], #(" XTR(SP_REG) " * 8)]                  \n\t"
		"    mov     sp, x30                                             \n\t"
		/* Restore SP*/
		"    ldr     x30,  [%[regs], #(" XTR(PC_REG) " * 8)]                  \n\t"
		/*"    msr     elr_el1, x2                                        \n\t"*/
        /* Restore cpsr condition bits  */
        "    ldr     x30, [%[regs]], #8                                       \n\t"
        /*"    msr     spsr_el1, x30                                       \n\t"*/
		"    mov     x30,%[regs]                                            \n\t"
        /* Restore registers */
		"    ldp     x0, x1, [x30], #16                                 \n\t"
		"    ldp     x2, x3, [x30], #16                                 \n\t"
		"    ldp     x4, x5, [x30], #16                                 \n\t"
		"    ldp     x6, x7, [x30], #16                                 \n\t"
		"    ldp     x8, x9, [x30], #16                                 \n\t"
		"    ldp     x10, x11, [x30], #16                               \n\t"
		"    ldp     x12, x13, [x30], #16                               \n\t"
		"    ldp     x14, x15, [x30], #16                               \n\t"
		"    ldp     x16, x17, [x30], #16                               \n\t"
		"    ldp     x18, x19, [x30], #16                               \n\t"
		"    ldp     x20, x21, [x30], #16                               \n\t"
		"    ldp     x22, x23, [x30], #16                               \n\t"
		"    ldp     x24, x25, [x30], #16                               \n\t"
		"    ldp     x26, x27, [x30], #16                               \n\t"
		"    ldp     x28, x29, [x30], #32                               \n\t"
		"    ldr     x30, [x30]		                                    \n\t"
		"    br      x30 		                                        \n\t"
        "disp_resume_context_epilog:                                    \n\t"
        "    mov     x0, x0          ; nop                              \n\t"
        :: [disp] "r" (disp), [regs] "r" (regs));
}


/* This function assumes that the register state is pristine, including the
 * stack pointer.  This assumption is violated if, for example, its argument
 * is spilled onto the stack.  XXX - We are relying on the optimiser to
 * eliminate any such register spills, and give us what is essentially a naked
 * function (which don't exist in AArch64 GCC). */
static void __attribute__((noinline, optimize(2)))
disp_save_context(uint64_t *regs)
{
    __asm volatile(
		"    mov     x19, %[regs]                                        \n\t"
		"	 str	 x1, [x19], #8										 \n\t"
		"    stp     x0, x1, [x19], #16                                  \n\t"
		"    stp     x2, x3, [x19], #16                                  \n\t"
		"    stp     x4, x5, [x19], #16                                  \n\t"
		"    stp     x6, x7, [x19], #16                                  \n\t"
		"    stp     x8, x9, [x19], #16                                  \n\t"
		"    stp     x10, x11, [x19], #16                                \n\t"
		"    stp     x12, x13, [x19], #16                                \n\t"
		"    stp     x14, x15, [x19], #16                                \n\t"
		"    stp     x16, x17, [x19], #16                                \n\t"
		"    stp     x18, x19, [x19], #16                                \n\t"
		"    stp     x20, x21, [x19], #16                                \n\t"
		"    stp     x22, x23, [x19], #16                                \n\t"
		"    stp     x24, x25, [x19], #16                                \n\t"
		"    stp     x26, x27, [x19], #16                                \n\t"
		"    stp     x28, x29, [x19], #16                                \n\t"
		"    str     x30, [x19], #8                                      \n\t"
		"	 mov	 x2, sp												 \n\t"
		"    str     x2, [x19], #8                                       \n\t"
		"    adr     x2, disp_save_context_resume                        \n\t"
        "    str     x2, [x19]  							             \n\t"
        "disp_save_context_resume:                                       \n\t"
        "    br       x30                                                \n\t"
        :: [regs] "r" (regs));
}

///////////////////////////////////////////////////////////////////////////////

/**
 * \brief Resume execution of a given register state
 *
 * This function resumes the execution of the given register state on the
 * current dispatcher. It may only be called while the dispatcher is disabled.
 *
 * \param disp Current dispatcher pointer
 * \param regs Register state snapshot
 */
void
disp_resume(dispatcher_handle_t handle,
            arch_registers_state_t *archregs)

{
    struct dispatcher_shared_aarch64 *disp =
        get_dispatcher_shared_aarch64(handle);

    // The definition of disp_resume_end is a totally flakey. The system
    // uses the location of the PC to determine where to spill the thread
    // context for exceptions and interrupts. There are two safe ways of doing
    // this:
    //
    // 1) Write this entire function in assmebler.
    // 2) Write this function in C and write a linker script to emit
    //    function bounds.

    assert_disabled(curdispatcher() == handle);
    assert_disabled(disp->d.disabled);
    assert_disabled(disp->d.haswork);

#ifdef CONFIG_DEBUG_DEADLOCKS
    ((struct disp_priv *)disp)->yieldcount = 0;
#endif

    disp_resume_context(&disp->d, archregs->regs);
}

/**
 * \brief Switch execution between two register states
 *
 * This function saves as much as necessary of the current register state
 * (which, when resumed will return to the caller), and switches execution
 * by resuming the given register state.  It may only be called while the
 * dispatcher is disabled.
 *
 * \param disp Current dispatcher pointer
 * \param from_regs Location to save current register state
 * \param to_regs Location from which to resume new register state
 */
void disp_switch(dispatcher_handle_t handle,
                 arch_registers_state_t *from_state,
                 arch_registers_state_t *to_state)
{
    struct dispatcher_shared_aarch64 *disp =
        get_dispatcher_shared_aarch64(handle);

    assert_disabled(curdispatcher() == handle);
    assert_disabled(disp->d.disabled);
    assert_disabled(disp->d.haswork);
    assert_disabled(to_state != NULL);

    disp_save_context(from_state->regs);
    from_state->named.pc = (lvaddr_t)disp_switch_epilog;
    disp_resume_context(&disp->d, to_state->regs);

    __asm volatile("disp_switch_epilog:");
}

/**
 * \brief Save the current register state and optionally yield the CPU
 *
 * This function saves as much as necessary of the current register state
 * (which, when resumed will return to the caller), and then either
 * re-enters the thread scheduler or yields the CPU.
 * It may only be called while the dispatcher is disabled.
 *
 * \param disp Current dispatcher pointer
 * \param regs Location to save current register state
 * \param yield If true, yield CPU to kernel; otherwise re-run thread scheduler
 * \param yield_to Endpoint capability for dispatcher to which we want to yield
 */
void disp_save(dispatcher_handle_t handle,
               arch_registers_state_t *state,
               bool yield, capaddr_t yield_to)
{
    struct dispatcher_shared_aarch64 *disp =
        get_dispatcher_shared_aarch64(handle);

    assert_disabled(curdispatcher() == handle);
    assert_disabled(disp->d.disabled);

    disp_save_context(state->regs);
    state->named.pc = (lvaddr_t)disp_save_epilog;

    if (yield) {
        sys_yield(yield_to);
        // may fail if target doesn't exist; if so, just fall through
    }
    // this code won't run if the yield succeeded

    // enter thread scheduler again
    // this doesn't return, and will call disp_yield if there's nothing to do
    thread_run_disabled(handle);

    __asm volatile("disp_save_epilog:");
}

void disp_save_rm_kcb(void)
{
    dispatcher_handle_t handle = disp_disable();
    struct dispatcher_shared_aarch64 *disp =
        get_dispatcher_shared_aarch64(handle);
    arch_registers_state_t *state =
        dispatcher_get_enabled_save_area(handle);

    assert_disabled(curdispatcher() == handle);
    assert_disabled(disp->d.disabled);

    disp_save_context(state->regs);
    state->named.pc = (lvaddr_t)disp_save_rm_kcb_epilog;

    sys_suspend(false);

    // enter thread scheduler again
    // this doesn't return, and will call disp_yield if there's nothing to do
    thread_run_disabled(handle);

    __asm volatile("disp_save_rm_kcb_epilog:");
}


/**
 * \brief Architecture-specific dispatcher initialisation
 */
void disp_arch_init(dispatcher_handle_t handle)
{
    struct dispatcher_shared_aarch64 *disp =
        get_dispatcher_shared_aarch64(handle);

    disp->d.dispatcher_run                = (lvaddr_t)run_entry;
    disp->d.dispatcher_pagefault          = (lvaddr_t)pagefault_entry;
    disp->d.dispatcher_pagefault_disabled = (lvaddr_t)disabled_pagefault_entry;
    disp->d.dispatcher_trap               = (lvaddr_t)trap_entry;
    disp->crit_pc_low                     = (lvaddr_t)disp_resume_context;
    disp->crit_pc_high                    = (lvaddr_t)disp_resume_context_epilog;
}
