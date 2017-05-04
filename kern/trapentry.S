/* See COPYRIGHT for copyright information. */

#include <inc/mmu.h>
#include <inc/memlayout.h>
#include <inc/trap.h>

#include <kern/picirq.h>


###################################################################
# exceptions/interrupts
###################################################################

/* TRAPHANDLER defines a globally-visible function for handling a trap.
 * It pushes a trap number onto the stack, then jumps to _alltraps.
 * Use TRAPHANDLER for traps where the CPU automatically pushes an error code.
 *
 * You shouldn't call a TRAPHANDLER function from C, but you may
 * need to _declare_ one in C (for instance, to get a function pointer
 * during IDT setup).  You can declare the function with
 *   void NAME();
 * where NAME is the argument passed to TRAPHANDLER.
 */
#define TRAPHANDLER(name, num, cpl)						\
	.globl name;		/* define global symbol for 'name' */	\
	.type name, @function;	/* symbol type is function */		\
	.align 2;		/* align function definition */		\
	name:			/* function starts here */		\
	pushl $(num);							\
	jmp _alltraps;							\
	.data;								\
	.byte num;							\
	.byte cpl;							\
	.int name;								\
	.text;

/* Use TRAPHANDLER_NOEC for traps where the CPU doesn't push an error code.
 * It pushes a 0 in place of the error code, so the trap frame has the same
 * format in either case.
 */
#define TRAPHANDLER_NOEC(name, num, cpl)					\
	.globl name;							\
	.type name, @function;						\
	.align 2;							\
	name:								\
	pushl $0;							\
	pushl $(num);							\
	jmp _alltraps;							\
	.data;								\
	.byte num;							\
	.byte cpl;							\
	.int name;								\
	.text;

.data
.globl trapentries
trapentries:


.text

/*
 * Lab 3: Your code here for generating entry points for the different traps.
 */
 
/*
 * Processor Defined Traps (0-31)
*/
TRAPHANDLER_NOEC(t_divide, T_DIVIDE, 0)
TRAPHANDLER_NOEC(t_debug, T_DEBUG, 0)
TRAPHANDLER_NOEC(t_nmi, T_NMI, 0)
TRAPHANDLER_NOEC(t_brkpt, T_BRKPT, 3)
TRAPHANDLER_NOEC(t_oflow, T_OFLOW, 0)
TRAPHANDLER_NOEC(t_bound, T_BOUND, 0)
TRAPHANDLER_NOEC(t_illop, T_ILLOP, 0)
TRAPHANDLER_NOEC(t_device, T_DEVICE, 0)
TRAPHANDLER(t_dblflt, T_DBLFLT, 0)
TRAPHANDLER(t_tss, T_TSS, 0)
TRAPHANDLER(t_segnp, T_SEGNP, 0)
TRAPHANDLER(t_stack, T_STACK, 0)
TRAPHANDLER(t_gpflt, T_GPFLT, 0)
TRAPHANDLER(t_pgflt, T_PGFLT, 0)
TRAPHANDLER_NOEC(t_fperr, T_FPERR, 0)
TRAPHANDLER(t_align, T_ALIGN, 0)
TRAPHANDLER_NOEC(t_mchk, T_MCHK, 0)
TRAPHANDLER_NOEC(t_simderr, T_SIMDERR, 0)

/*
 * Custom Interrupts/Traps (32-254)
*/
TRAPHANDLER_NOEC(t_syscall, T_SYSCALL, 3)

.data

.byte 0
.byte 0
.int 0

.text

/*
 * Lab 3: Your code here for _alltraps
 */
_alltraps:
	push %DS
	push %ES
	pushal
	mov $GD_KD, %EAX
	movl %EAX, %DS
	movl %EAX, %ES
	push %esp
	
	call trap
