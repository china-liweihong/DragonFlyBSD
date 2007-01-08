/*-
 * Copyright (c) 1982, 1986 The Regents of the University of California.
 * Copyright (c) 1989, 1990 William Jolitz
 * Copyright (c) 1994 John Dyson
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department, and William Jolitz.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)vm_machdep.c	7.3 (Berkeley) 5/13/91
 *	Utah $Hdr: vm_machdep.c 1.16.1.1 89/06/23$
 * $FreeBSD: src/sys/i386/i386/vm_machdep.c,v 1.132.2.9 2003/01/25 19:02:23 dillon Exp $
 * $DragonFly: src/sys/platform/vkernel/i386/vm_machdep.c,v 1.3 2007/01/08 03:33:43 dillon Exp $
 */

#include "use_npx.h"
#include "use_isa.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/interrupt.h>
#include <sys/vnode.h>
#include <sys/vmmeter.h>
#include <sys/kernel.h>
#include <sys/sysctl.h>
#include <sys/unistd.h>

#include <machine/clock.h>
#include <machine/cpu.h>
#include <machine/md_var.h>
#include <machine/smp.h>
#include <machine/pcb.h>
#include <machine/pcb_ext.h>
#include <machine/vm86.h>
#include <machine/segments.h>
#include <machine/globaldata.h>	/* npxthread */

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <sys/lock.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <vm/vm_map.h>
#include <vm/vm_extern.h>

#include <sys/user.h>
#include <sys/thread2.h>

#include <bus/isa/i386/isa.h>

#include <stdio.h>
#include <stdlib.h>

char machine[] = MACHINE_CPU;
SYSCTL_STRING(_hw, HW_MACHINE, machine, CTLFLAG_RD,
	      machine, 0, "Machine class");

char cpu_vendor[] = "DragonFly";	/* XXX */
u_int cpu_id = 0x80000000;		/* XXX */

/*
 * Finish a fork operation, with lwp lp2 nearly set up.
 * Copy and update the pcb, set up the stack so that the child
 * ready to run and return to user mode.
 */
void
cpu_fork(struct lwp *lp1, struct lwp *lp2, int flags)
{
	struct pcb *pcb2;

	if ((flags & RFPROC) == 0) {
		if ((flags & RFMEM) == 0) {
			/* unshare user LDT */
			struct pcb *pcb1 = lp1->lwp_thread->td_pcb;
			struct pcb_ldt *pcb_ldt = pcb1->pcb_ldt;
			if (pcb_ldt && pcb_ldt->ldt_refcnt > 1) {
				pcb_ldt = user_ldt_alloc(pcb1,pcb_ldt->ldt_len);
				user_ldt_free(pcb1);
				pcb1->pcb_ldt = pcb_ldt;
				set_user_ldt(pcb1);
			}
		}
		return;
	}

#if NNPX > 0
	/* Ensure that lp1's pcb is up to date. */
	if (mdcpu->gd_npxthread == lp1->lwp_thread)
		npxsave(lp1->lwp_thread->td_savefpu);
#endif
	
	/*
	 * Copy lp1's PCB.  This really only applies to the
	 * debug registers and FP state, but its faster to just copy the
	 * whole thing.  Because we only save the PCB at switchout time,
	 * the register state may not be current.
	 */
	pcb2 = lp2->lwp_thread->td_pcb;
	*pcb2 = *lp1->lwp_thread->td_pcb;

	/*
	 * Create a new fresh stack for the new process.
	 * Copy the trap frame for the return to user mode as if from a
	 * syscall.  This copies the user mode register values.  The
	 * 16 byte offset saves space for vm86, and must match 
	 * common_tss.esp0 (kernel stack pointer on entry from user mode)
	 *
	 * pcb_esp must allocate an additional call-return pointer below
	 * the trap frame which will be restored by cpu_restore from
	 * PCB_EIP, and the thread's td_sp pointer must allocate an
	 * additonal two worsd below the pcb_esp call-return pointer to
	 * hold the LWKT restore function pointer and eflags.
	 *
	 * The LWKT restore function pointer must be set to cpu_restore,
	 * which is our standard heavy weight process switch-in function.
	 * YYY eventually we should shortcut fork_return and fork_trampoline
	 * to use the LWKT restore function directly so we can get rid of
	 * all the extra crap we are setting up.
	 */
	lp2->lwp_md.md_regs = (struct trapframe *)((char *)pcb2 - 16) - 1;
	bcopy(lp1->lwp_md.md_regs, lp2->lwp_md.md_regs, sizeof(*lp2->lwp_md.md_regs));

	/*
	 * Set registers for trampoline to user mode.  Leave space for the
	 * return address on stack.  These are the kernel mode register values.
	 */
	pcb2->pcb_cr3 = vtophys(vmspace_pmap(lp2->lwp_proc->p_vmspace)->pm_pdir);
	pcb2->pcb_edi = 0;
	pcb2->pcb_esi = (int)fork_return;	/* fork_trampoline argument */
	pcb2->pcb_ebp = 0;
	pcb2->pcb_esp = (int)lp2->lwp_md.md_regs - sizeof(void *);
	pcb2->pcb_ebx = (int)lp2;		/* fork_trampoline argument */
	pcb2->pcb_eip = (int)fork_trampoline;
	lp2->lwp_thread->td_sp = (char *)(pcb2->pcb_esp - sizeof(void *));
	*(u_int32_t *)lp2->lwp_thread->td_sp = PSL_USER;
	lp2->lwp_thread->td_sp -= sizeof(void *);
	*(void **)lp2->lwp_thread->td_sp = (void *)cpu_heavy_restore;

	/*
	 * pcb2->pcb_ldt:	duplicated below, if necessary.
	 * pcb2->pcb_savefpu:	cloned above.
	 * pcb2->pcb_flags:	cloned above (always 0 here?).
	 * pcb2->pcb_onfault:	cloned above (always NULL here?).
	 */

	/*
	 * XXX don't copy the i/o pages.  this should probably be fixed.
	 */
	pcb2->pcb_ext = 0;

        /* Copy the LDT, if necessary. */
        if (pcb2->pcb_ldt != 0) {
		if (flags & RFMEM) {
			pcb2->pcb_ldt->ldt_refcnt++;
		} else {
			pcb2->pcb_ldt = user_ldt_alloc(pcb2,
				pcb2->pcb_ldt->ldt_len);
		}
        }
	bcopy(&lp1->lwp_thread->td_tls, &lp2->lwp_thread->td_tls,
	      sizeof(lp2->lwp_thread->td_tls));
	/*
	 * Now, cpu_switch() can schedule the new process.
	 * pcb_esp is loaded pointing to the cpu_switch() stack frame
	 * containing the return address when exiting cpu_switch.
	 * This will normally be to fork_trampoline(), which will have
	 * %ebx loaded with the new proc's pointer.  fork_trampoline()
	 * will set up a stack to call fork_return(p, frame); to complete
	 * the return to user-mode.
	 */
}

/*
 * Intercept the return address from a freshly forked process that has NOT
 * been scheduled yet.
 *
 * This is needed to make kernel threads stay in kernel mode.
 */
void
cpu_set_fork_handler(struct lwp *lp, void (*func)(void *), void *arg)
{
	/*
	 * Note that the trap frame follows the args, so the function
	 * is really called like this:  func(arg, frame);
	 */
	lp->lwp_thread->td_pcb->pcb_esi = (int) func;	/* function */
	lp->lwp_thread->td_pcb->pcb_ebx = (int) arg;	/* first arg */
}

void
cpu_set_thread_handler(thread_t td, void (*rfunc)(void), void *func, void *arg)
{
	td->td_pcb->pcb_esi = (int)func;
	td->td_pcb->pcb_ebx = (int) arg;
	td->td_switch = cpu_lwkt_switch;
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = rfunc;	/* exit function on return */
	td->td_sp -= sizeof(void *);
	*(void **)td->td_sp = cpu_kthread_restore;
}

void
cpu_proc_exit(void)
{
	struct thread *td = curthread;
	struct pcb *pcb;
	struct pcb_ext *ext;

#if NNPX > 0
	npxexit();
#endif	/* NNPX */

	/*
	 * If we were using a private TSS do a forced-switch to ourselves
	 * to switch back to the common TSS before freeing it.
	 */
	pcb = td->td_pcb;
	if ((ext = pcb->pcb_ext) != NULL) {
		crit_enter();
		pcb->pcb_ext = NULL;
		td->td_switch(td);
		crit_exit();
		kmem_free(&kernel_map, (vm_offset_t)ext, ctob(IOPAGES + 1));
	}
	user_ldt_free(pcb);
        if (pcb->pcb_flags & PCB_DBREGS) {
                /*
                 * disable all hardware breakpoints
                 */
                reset_dbregs();
                pcb->pcb_flags &= ~PCB_DBREGS;
        }
	td->td_gd->gd_cnt.v_swtch++;

	crit_enter_quick(td);
	lwkt_deschedule_self(td);
	lwkt_remove_tdallq(td);
	cpu_thread_exit();
}

/*
 * Terminate the current thread.  The caller must have already acquired
 * the thread's rwlock and placed it on a reap list or otherwise notified
 * a reaper of its existance.  We set a special assembly switch function which
 * releases td_rwlock after it has cleaned up the MMU state and switched
 * out the stack.
 *
 * Must be caller from a critical section and with the thread descheduled.
 */
void
cpu_thread_exit(void)
{
	curthread->td_switch = cpu_exit_switch;
	curthread->td_flags |= TDF_EXITING;
	lwkt_switch();
	panic("cpu_exit");
}

/*
 * Process Reaper.  Called after the caller has acquired the thread's
 * rwlock and removed it from the reap list.
 */
void
cpu_proc_wait(struct proc *p)
{
	struct thread *td;

	/* drop per-process resources */
	td = pmap_dispose_proc(p);
	if (td)
		lwkt_free_thread(td);
}

/*
 * Dump the machine specific header information at the start of a core dump.
 */
int
cpu_coredump(struct thread *td, struct vnode *vp, struct ucred *cred)
{
	struct proc *p = td->td_proc;
	int error;
	caddr_t tempuser;

	KKASSERT(p);
	tempuser = kmalloc(ctob(UPAGES), M_TEMP, M_WAITOK);
	if (!tempuser)
		return EINVAL;
	
	bzero(tempuser, ctob(UPAGES));
	bcopy(p->p_addr, tempuser, sizeof(struct user));
	bcopy(p->p_md.md_regs,
	      tempuser + ((caddr_t) p->p_md.md_regs - (caddr_t) p->p_addr),
	      sizeof(struct trapframe));
	bcopy(p->p_thread->td_pcb, tempuser + ((char *)p->p_thread->td_pcb - (char *)p->p_addr), sizeof(struct pcb));

	error = vn_rdwr(UIO_WRITE, vp, (caddr_t) tempuser, ctob(UPAGES),
			(off_t)0, UIO_SYSSPACE, IO_UNIT, cred, (int *)NULL);

	kfree(tempuser, M_TEMP);
	
	return error;
}

#ifdef notyet
static void
setredzone(u_short *pte, caddr_t vaddr)
{
/* eventually do this by setting up an expand-down stack segment
   for ss0: selector, allowing stack access down to top of u.
   this means though that protection violations need to be handled
   thru a double fault exception that must do an integral task
   switch to a known good context, within which a dump can be
   taken. a sensible scheme might be to save the initial context
   used by sched (that has physical memory mapped 1:1 at bottom)
   and take the dump while still in mapped mode */
}
#endif

/*
 * Convert kernel VA to physical address
 */
vm_paddr_t
kvtop(void *addr)
{
	vm_paddr_t pa;

	pa = pmap_kextract((vm_offset_t)addr);
	if (pa == 0)
		panic("kvtop: zero page frame");
	return (pa);
}

int
grow_stack(struct proc *p, u_int sp)
{
	int rv;

	rv = vm_map_growstack (p, sp);
	if (rv != KERN_SUCCESS)
		return (0);

	return (1);
}

SYSCTL_DECL(_vm_stats_misc);

static int cnt_prezero;

SYSCTL_INT(_vm_stats_misc, OID_AUTO,
	cnt_prezero, CTLFLAG_RD, &cnt_prezero, 0, "");

/*
 * Tell whether this address is in some physical memory region.
 * Currently used by the kernel coredump code in order to avoid
 * dumping the ``ISA memory hole'' which could cause indefinite hangs,
 * or other unpredictable behaviour.
 */

int
is_physical_memory(vm_offset_t addr)
{
	return 1;
}

