/*-
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/i386/include/atomic.h,v 1.9.2.1 2000/07/07 00:38:47 obrien Exp $
 * $DragonFly: src/sys/cpu/i386/include/atomic.h,v 1.25 2008/06/26 23:06:50 dillon Exp $
 */
#ifndef _CPU_ATOMIC_H_
#define _CPU_ATOMIC_H_

#ifndef _SYS_TYPES_H_
#include <sys/types.h>
#endif

/*
 * Various simple arithmetic on memory which is atomic in the presence
 * of interrupts and multiple processors.
 *
 * atomic_set_char(P, V)	(*(u_char*)(P) |= (V))
 * atomic_clear_char(P, V)	(*(u_char*)(P) &= ~(V))
 * atomic_add_char(P, V)	(*(u_char*)(P) += (V))
 * atomic_subtract_char(P, V)	(*(u_char*)(P) -= (V))
 *
 * atomic_set_short(P, V)	(*(u_short*)(P) |= (V))
 * atomic_clear_short(P, V)	(*(u_short*)(P) &= ~(V))
 * atomic_add_short(P, V)	(*(u_short*)(P) += (V))
 * atomic_subtract_short(P, V)	(*(u_short*)(P) -= (V))
 *
 * atomic_set_int(P, V)		(*(u_int*)(P) |= (V))
 * atomic_clear_int(P, V)	(*(u_int*)(P) &= ~(V))
 * atomic_add_int(P, V)		(*(u_int*)(P) += (V))
 * atomic_subtract_int(P, V)	(*(u_int*)(P) -= (V))
 *
 * atomic_set_long(P, V)	(*(u_long*)(P) |= (V))
 * atomic_clear_long(P, V)	(*(u_long*)(P) &= ~(V))
 * atomic_add_long(P, V)	(*(u_long*)(P) += (V))
 * atomic_subtract_long(P, V)	(*(u_long*)(P) -= (V))
 */

/*
 * The above functions are expanded inline in the statically-linked
 * kernel.  Lock prefixes are generated if an SMP kernel is being
 * built, or if user code is using these functions.
 *
 * Kernel modules call real functions which are built into the kernel.
 * This allows kernel modules to be portable between UP and SMP systems.
 */
#if defined(KLD_MODULE)
#define ATOMIC_ASM(NAME, TYPE, OP, V)			\
	extern void atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v); \
	extern void atomic_##NAME##_##TYPE##_nonlocked(volatile u_##TYPE *p, u_##TYPE v);
#else /* !KLD_MODULE */
#if defined(SMP) || !defined(_KERNEL)
#define MPLOCKED	"lock ; "
#else
#define MPLOCKED
#endif

/*
 * The assembly is volatilized to demark potential before-and-after side
 * effects if an interrupt or SMP collision were to occur.  The primary
 * atomic instructions are MP safe, the nonlocked instructions are 
 * local-interrupt-safe (so we don't depend on C 'X |= Y' generating an
 * atomic instruction).
 *
 * +m - memory is read and written (=m - memory is only written)
 * iq - integer constant or %ax/%bx/%cx/%dx (ir = int constant or any reg)
 *	(Note: byte instructions only work on %ax,%bx,%cx, or %dx).  iq
 *	is good enough for our needs so don't get fancy.
 */

/* egcs 1.1.2+ version */
#define ATOMIC_ASM(NAME, TYPE, OP, V)			\
static __inline void					\
atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(MPLOCKED OP			\
			 : "+m" (*p)			\
			 : "iq" (V)); 			\
}							\
static __inline void					\
atomic_##NAME##_##TYPE##_nonlocked(volatile u_##TYPE *p, u_##TYPE v)\
{							\
	__asm __volatile(OP				\
			 : "+m" (*p)			\
			 : "iq" (V)); 			\
}

#endif /* KLD_MODULE */

/* egcs 1.1.2+ version */
ATOMIC_ASM(set,	     char,  "orb %b1,%0",   v)
ATOMIC_ASM(clear,    char,  "andb %b1,%0", ~v)
ATOMIC_ASM(add,	     char,  "addb %b1,%0",  v)
ATOMIC_ASM(subtract, char,  "subb %b1,%0",  v)

ATOMIC_ASM(set,	     short, "orw %w1,%0",   v)
ATOMIC_ASM(clear,    short, "andw %w1,%0", ~v)
ATOMIC_ASM(add,	     short, "addw %w1,%0",  v)
ATOMIC_ASM(subtract, short, "subw %w1,%0",  v)

ATOMIC_ASM(set,	     int,   "orl %1,%0",   v)
ATOMIC_ASM(clear,    int,   "andl %1,%0", ~v)
ATOMIC_ASM(add,	     int,   "addl %1,%0",  v)
ATOMIC_ASM(subtract, int,   "subl %1,%0",  v)

ATOMIC_ASM(set,	     long,  "orl %1,%0",   v)
ATOMIC_ASM(clear,    long,  "andl %1,%0", ~v)
ATOMIC_ASM(add,	     long,  "addl %1,%0",  v)
ATOMIC_ASM(subtract, long,  "subl %1,%0",  v)

/*
 * atomic_poll_acquire_int(P)	Returns non-zero on success, 0 if the lock
 *				has already been acquired.
 * atomic_poll_release_int(P)
 *
 * These support the NDIS driver and are also used for IPIQ interlocks
 * between cpus.  Both the acquisition and release must be 
 * cache-synchronizing instructions.
 */

#if defined(KLD_MODULE)

extern int atomic_swap_int(volatile int *addr, int value);
extern int atomic_poll_acquire_int(volatile u_int *p);
extern void atomic_poll_release_int(volatile u_int *p);

#else

static __inline int
atomic_swap_int(volatile int *addr, int value)
{
	__asm __volatile("xchgl %0, %1" :
	    "=r" (value), "=m" (*addr) : "0" (value) : "memory");
	return (value);
}

static __inline
int
atomic_poll_acquire_int(volatile u_int *p)
{
	u_int data;

	__asm __volatile(MPLOCKED "btsl $0,%0; setnc %%al; andl $255,%%eax" : "+m" (*p), "=a" (data));
	return(data);
}

static __inline
void
atomic_poll_release_int(volatile u_int *p)
{
	__asm __volatile(MPLOCKED "btrl $0,%0" : "+m" (*p));
}

#endif

/*
 * These functions operate on a 32 bit interrupt interlock which is defined
 * as follows:
 *
 *	bit 0-30	interrupt handler disabled bits (counter)
 *	bit 31		interrupt handler currently running bit (1 = run)
 *
 * atomic_intr_cond_test(P)	Determine if the interlock is in an
 *				acquired state.  Returns 0 if it not
 *				acquired, non-zero if it is.
 *
 * atomic_intr_cond_try(P)
 *				Increment the request counter and attempt to
 *				set bit 31 to acquire the interlock.  If
 *				we are unable to set bit 31 the request
 *				counter is decremented and we return -1,
 *				otherwise we return 0.
 *
 * atomic_intr_cond_enter(P, func, arg)
 *				Increment the request counter and attempt to
 *				set bit 31 to acquire the interlock.  If
 *				we are unable to set bit 31 func(arg) is
 *				called in a loop until we are able to set
 *				bit 31.
 *
 * atomic_intr_cond_exit(P, func, arg)
 *				Decrement the request counter and clear bit
 *				31.  If the request counter is still non-zero
 *				call func(arg) once.
 *
 * atomic_intr_handler_disable(P)
 *				Set bit 30, indicating that the interrupt
 *				handler has been disabled.  Must be called
 *				after the hardware is disabled.
 *
 *				Returns bit 31 indicating whether a serialized
 *				accessor is active (typically the interrupt
 *				handler is running).  0 == not active,
 *				non-zero == active.
 *
 * atomic_intr_handler_enable(P)
 *				Clear bit 30, indicating that the interrupt
 *				handler has been enabled.  Must be called
 *				before the hardware is actually enabled.
 *
 * atomic_intr_handler_is_enabled(P)
 *				Returns bit 30, 0 indicates that the handler
 *				is enabled, non-zero indicates that it is
 *				disabled.  The request counter portion of
 *				the field is ignored.
 */

#if defined(KLD_MODULE)

void atomic_intr_init(__atomic_intr_t *p);
int atomic_intr_handler_disable(__atomic_intr_t *p);
void atomic_intr_handler_enable(__atomic_intr_t *p);
int atomic_intr_handler_is_enabled(__atomic_intr_t *p);
int atomic_intr_cond_test(__atomic_intr_t *p);
int atomic_intr_cond_try(__atomic_intr_t *p);
void atomic_intr_cond_enter(__atomic_intr_t *p, void (*func)(void *), void *arg);
void atomic_intr_cond_exit(__atomic_intr_t *p, void (*func)(void *), void *arg);

#else

static __inline
void
atomic_intr_init(__atomic_intr_t *p)
{
	*p = 0;
}

static __inline
int
atomic_intr_handler_disable(__atomic_intr_t *p)
{
	int data;

	__asm __volatile(MPLOCKED "orl $0x40000000,%1; movl %1,%%eax; " \
				  "andl $0x80000000,%%eax" \
				  : "=a"(data) , "+m"(*p));
	return(data);
}

static __inline
void
atomic_intr_handler_enable(__atomic_intr_t *p)
{
	__asm __volatile(MPLOCKED "andl $0xBFFFFFFF,%0" : "+m" (*p));
}

static __inline
int
atomic_intr_handler_is_enabled(__atomic_intr_t *p)
{
	int data;

	__asm __volatile("movl %1,%%eax; andl $0x40000000,%%eax" \
			 : "=a"(data) : "m"(*p));
	return(data);
}

static __inline
void
atomic_intr_cond_enter(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "incl %0; " \
			 "1: ;" \
			 MPLOCKED "btsl $31,%0; jnc 2f; " \
			 "pushl %2; call *%1; addl $4,%%esp; " \
			 "jmp 1b; " \
			 "2: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx");
}

/*
 * Attempt to enter the interrupt condition variable.  Returns zero on
 * success, 1 on failure.
 */
static __inline
int
atomic_intr_cond_try(__atomic_intr_t *p)
{
	int ret;

	__asm __volatile(MPLOCKED "incl %0; " \
			 "1: ;" \
			 "subl %%eax,%%eax; " \
			 MPLOCKED "btsl $31,%0; jnc 2f; " \
			 MPLOCKED "decl %0; " \
			 "movl $1,%%eax;" \
			 "2: ;" \
			 : "+m" (*p), "=a"(ret) \
#ifdef __clang__
                         : : "ax", "cx", "dx");
#else
                         : : "cx", "dx");
#endif
	return (ret);
}


static __inline
int
atomic_intr_cond_test(__atomic_intr_t *p)
{
	return((int)(*p & 0x80000000));
}

static __inline
void
atomic_intr_cond_exit(__atomic_intr_t *p, void (*func)(void *), void *arg)
{
	__asm __volatile(MPLOCKED "decl %0; " \
			MPLOCKED "btrl $31,%0; " \
			"testl $0x3FFFFFFF,%0; jz 1f; " \
			 "pushl %2; call *%1; addl $4,%%esp; " \
			 "1: ;" \
			 : "+m" (*p) \
			 : "r"(func), "m"(arg) \
			 : "ax", "cx", "dx");
}

#endif

/*
 * Atomic compare and set
 *
 * if (*_dst == _old) *_dst = _new (all 32 bit words)
 *
 * Returns 0 on failure, non-zero on success
 */
#if defined(KLD_MODULE)

extern int atomic_cmpset_int(volatile u_int *_dst, u_int _old, u_int _new);
extern int atomic_cmpset_long(volatile u_long *dst, u_long exp, u_long src);
extern u_int atomic_fetchadd_int(volatile u_int *p, u_int v);

#else

static __inline int
atomic_cmpset_int(volatile u_int *_dst, u_int _old, u_int _new)
{
	int res = _old;

	__asm __volatile(MPLOCKED "cmpxchgl %2,%1; " \
			 "setz %%al; " \
			 "movzbl %%al,%0; " \
			 : "+a" (res), "=m" (*_dst) \
			 : "r" (_new), "m" (*_dst) \
			 : "memory");
	return res;
}

static __inline int
atomic_cmpset_long(volatile u_long *dst, u_long exp, u_long src)
{
	 return (atomic_cmpset_int((volatile u_int *)dst, (u_int)exp,
				   (u_int)src));
}

/*
 * Atomically add the value of v to the integer pointed to by p and return
 * the previous value of *p.
 */
static __inline u_int
atomic_fetchadd_int(volatile u_int *p, u_int v)
{
	__asm __volatile(MPLOCKED "xaddl %0,%1; " \
			 : "+r" (v), "=m" (*p)	\
			 : "m" (*p)		\
			 : "memory");
	return (v);
}

#endif	/* KLD_MODULE */

/* Acquire and release variants are identical to the normal ones. */
#define	atomic_set_acq_char		atomic_set_char
#define	atomic_set_rel_char		atomic_set_char
#define	atomic_clear_acq_char		atomic_clear_char
#define	atomic_clear_rel_char		atomic_clear_char
#define	atomic_add_acq_char		atomic_add_char
#define	atomic_add_rel_char		atomic_add_char
#define	atomic_subtract_acq_char	atomic_subtract_char
#define	atomic_subtract_rel_char	atomic_subtract_char

#define	atomic_set_acq_short		atomic_set_short
#define	atomic_set_rel_short		atomic_set_short
#define	atomic_clear_acq_short		atomic_clear_short
#define	atomic_clear_rel_short		atomic_clear_short
#define	atomic_add_acq_short		atomic_add_short
#define	atomic_add_rel_short		atomic_add_short
#define	atomic_subtract_acq_short	atomic_subtract_short
#define	atomic_subtract_rel_short	atomic_subtract_short

#define	atomic_set_acq_int		atomic_set_int
#define	atomic_set_rel_int		atomic_set_int
#define	atomic_clear_acq_int		atomic_clear_int
#define	atomic_clear_rel_int		atomic_clear_int
#define	atomic_add_acq_int		atomic_add_int
#define	atomic_add_rel_int		atomic_add_int
#define	atomic_subtract_acq_int		atomic_subtract_int
#define	atomic_subtract_rel_int		atomic_subtract_int
#define	atomic_cmpset_acq_int		atomic_cmpset_int
#define	atomic_cmpset_rel_int		atomic_cmpset_int

#define	atomic_set_acq_long		atomic_set_long
#define	atomic_set_rel_long		atomic_set_long
#define	atomic_clear_acq_long		atomic_clear_long
#define	atomic_clear_rel_long		atomic_clear_long
#define	atomic_add_acq_long		atomic_add_long
#define	atomic_add_rel_long		atomic_add_long
#define	atomic_subtract_acq_long	atomic_subtract_long
#define	atomic_subtract_rel_long	atomic_subtract_long
#define	atomic_cmpset_acq_long		atomic_cmpset_long
#define	atomic_cmpset_rel_long		atomic_cmpset_long

/* Operations on 8-bit bytes. */
#define	atomic_set_8		atomic_set_char
#define	atomic_set_acq_8	atomic_set_acq_char
#define	atomic_set_rel_8	atomic_set_rel_char
#define	atomic_clear_8		atomic_clear_char
#define	atomic_clear_acq_8	atomic_clear_acq_char
#define	atomic_clear_rel_8	atomic_clear_rel_char
#define	atomic_add_8		atomic_add_char
#define	atomic_add_acq_8	atomic_add_acq_char
#define	atomic_add_rel_8	atomic_add_rel_char
#define	atomic_subtract_8	atomic_subtract_char
#define	atomic_subtract_acq_8	atomic_subtract_acq_char
#define	atomic_subtract_rel_8	atomic_subtract_rel_char
#define	atomic_load_acq_8	atomic_load_acq_char
#define	atomic_store_rel_8	atomic_store_rel_char

/* Operations on 16-bit words. */
#define	atomic_set_16		atomic_set_short
#define	atomic_set_acq_16	atomic_set_acq_short
#define	atomic_set_rel_16	atomic_set_rel_short
#define	atomic_clear_16		atomic_clear_short
#define	atomic_clear_acq_16	atomic_clear_acq_short
#define	atomic_clear_rel_16	atomic_clear_rel_short
#define	atomic_add_16		atomic_add_short
#define	atomic_add_acq_16	atomic_add_acq_short
#define	atomic_add_rel_16	atomic_add_rel_short
#define	atomic_subtract_16	atomic_subtract_short
#define	atomic_subtract_acq_16	atomic_subtract_acq_short
#define	atomic_subtract_rel_16	atomic_subtract_rel_short
#define	atomic_load_acq_16	atomic_load_acq_short
#define	atomic_store_rel_16	atomic_store_rel_short

/* Operations on 32-bit double words. */
#define	atomic_set_32		atomic_set_int
#define	atomic_set_acq_32	atomic_set_acq_int
#define	atomic_set_rel_32	atomic_set_rel_int
#define	atomic_clear_32		atomic_clear_int
#define	atomic_clear_acq_32	atomic_clear_acq_int
#define	atomic_clear_rel_32	atomic_clear_rel_int
#define	atomic_add_32		atomic_add_int
#define	atomic_add_acq_32	atomic_add_acq_int
#define	atomic_add_rel_32	atomic_add_rel_int
#define	atomic_subtract_32	atomic_subtract_int
#define	atomic_subtract_acq_32	atomic_subtract_acq_int
#define	atomic_subtract_rel_32	atomic_subtract_rel_int
#define	atomic_load_acq_32	atomic_load_acq_int
#define	atomic_store_rel_32	atomic_store_rel_int
#define	atomic_cmpset_32	atomic_cmpset_int
#define	atomic_cmpset_acq_32	atomic_cmpset_acq_int
#define	atomic_cmpset_rel_32	atomic_cmpset_rel_int
#define	atomic_readandclear_32	atomic_readandclear_int
#define	atomic_fetchadd_32	atomic_fetchadd_int

/* Operations on pointers. */
#define	atomic_set_ptr(p, v) \
	atomic_set_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_acq_ptr(p, v) \
	atomic_set_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_set_rel_ptr(p, v) \
	atomic_set_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_ptr(p, v) \
	atomic_clear_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_acq_ptr(p, v) \
	atomic_clear_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_clear_rel_ptr(p, v) \
	atomic_clear_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_ptr(p, v) \
	atomic_add_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_acq_ptr(p, v) \
	atomic_add_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_add_rel_ptr(p, v) \
	atomic_add_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_ptr(p, v) \
	atomic_subtract_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_acq_ptr(p, v) \
	atomic_subtract_acq_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_subtract_rel_ptr(p, v) \
	atomic_subtract_rel_int((volatile u_int *)(p), (u_int)(v))
#define	atomic_load_acq_ptr(p) \
	atomic_load_acq_int((volatile u_int *)(p))
#define	atomic_store_rel_ptr(p, v) \
	atomic_store_rel_int((volatile u_int *)(p), (v))
#define	atomic_cmpset_ptr(dst, old, new) \
	atomic_cmpset_int((volatile u_int *)(dst), (u_int)(old), (u_int)(new))
#define	atomic_cmpset_acq_ptr(dst, old, new) \
	atomic_cmpset_acq_int((volatile u_int *)(dst), (u_int)(old), \
	    (u_int)(new))
#define	atomic_cmpset_rel_ptr(dst, old, new) \
	atomic_cmpset_rel_int((volatile u_int *)(dst), (u_int)(old), \
	    (u_int)(new))
#define	atomic_readandclear_ptr(p) \
	atomic_readandclear_int((volatile u_int *)(p))

#endif /* ! _CPU_ATOMIC_H_ */
