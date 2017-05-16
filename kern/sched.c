#include <inc/assert.h>
#include <inc/x86.h>
#include <kern/spinlock.h>
#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/monitor.h>

void sched_halt(void);

// Choose a user environment to run and run it.
void
sched_yield(void)
{
	int start, i, index;

	// Implement simple round-robin scheduling.
	//
	// Search through 'envs' for an ENV_RUNNABLE environment in
	// circular fashion starting just after the env this CPU was
	// last running.  Switch to the first such environment found.
	//
	// If no envs are runnable, but the environment previously
	// running on this CPU is still ENV_RUNNING, it's okay to
	// choose that environment.
	//
	// Never choose an environment that's currently running on
	// another CPU (env_status == ENV_RUNNING). If there are
	// no runnable environments, simply drop through to the code
	// below to halt the cpu.

	// LAB 4: Your code here.
	
	// This round robin scheduling is done by round-robining the 'envs'
	// array. we dont care about the environments ids!
	start = -1;
	struct Env *runme = NULL; // needed for lab4 challenge
	if (curenv != NULL)
		start = curenv - envs; // it works because of pointer arithmetics

	for (i = start + 1; i < start + NENV; i++) {
		// index for 'envs'. we do a cyclic iteration, from 'curenv'
		// or from the first env if curenv == NULL.
		index = i % NENV;
		/*
		if(index < 5) {
			cprintf("envs[%d].priority = %d, runnable = %x\n", index, envs[index].priority, envs[index].env_status == ENV_RUNNABLE);
		}
		*/
		// If we found a runnable env, run it. env_run will not return.
		if (envs[index].env_status == ENV_RUNNABLE) {
			if(runme == NULL || envs[index].priority > runme->priority){
				runme = &envs[index];
			}
		}
	}
	//cprintf("envs[%x].priority = %d, running = %x\n", start, envs[start].priority, envs[start].env_status == ENV_RUNNING);
	if ((start >= 0 && envs[start].env_status == ENV_RUNNING) && (runme == NULL || envs[start].priority > runme->priority)) {
			runme = &envs[start];
	}
	if (runme) {
		// cprintf("running env with priority %d\n", runme->priority);
		env_run(runme);
	}
	
	// we have nothing to do.
	// sched_halt never returns
	sched_halt();
}

// Halt this CPU when there is nothing to do. Wait until the
// timer interrupt wakes it up. This function never returns.
//
void
sched_halt(void)
{
	int i;

	// For debugging and testing purposes, if there are no runnable
	// environments in the system, then drop into the kernel monitor.
	for (i = 0; i < NENV; i++) {
		if ((envs[i].env_status == ENV_RUNNABLE ||
		     envs[i].env_status == ENV_RUNNING ||
		     envs[i].env_status == ENV_DYING))
			break;
	}
	if (i == NENV) {
		cprintf("No runnable environments in the system!\n");
		while (1)
			monitor(NULL);
	}

	// Mark that no environment is running on this CPU
	curenv = NULL;
	lcr3(PADDR(kern_pgdir));

	// Mark that this CPU is in the HALT state, so that when
	// timer interupts come in, we know we should re-acquire the
	// big kernel lock
	xchg(&thiscpu->cpu_status, CPU_HALTED);

	// Release the big kernel lock as if we were "leaving" the kernel
	unlock_kernel();

	// Reset stack pointer, enable interrupts and then halt.
	asm volatile (
		"movl $0, %%ebp\n"
		"movl %0, %%esp\n"
		"pushl $0\n"
		"pushl $0\n"
		"sti\n"
		"1:\n"
		"hlt\n"
		"jmp 1b\n"
	: : "a" (thiscpu->cpu_ts.ts_esp0));
}

