// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "showmapping", "showmapping <start_addr> <end_addr>", mon_showmapping },
	{ "editmapping", "editmapping <va> <pte>", mon_editmapping },
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

/**
 * mon_showmapping : list page entries in specified range of virtual addresses
 */
int
mon_showmapping(int argc, char **argv, struct Trapframe *tf)
{
	const int output_limit = 10;
	bool show_non_present = false;
	int count_output;
	pde_t *pgdir;
	pte_t *pte;
	char *addr1, *addr2;
	long va;
	
	// Access the page directory through loopback
	pgdir = (pde_t *) (UVPT | ((UVPT >> PDXSHIFT) << PTXSHIFT));
	
	// We should have 3 arguments: command name, addr1, addr2.
	if (argc != 3)
		return 1;
	
	// Retrieve the virtual addresses of the relevant pages as intptr_t
	addr1 = (char *) ROUNDDOWN((int) strtol(argv[1], NULL, 16), PGSIZE);
	addr2 = (char *) ROUNDDOWN((int) strtol(argv[2], NULL, 16), PGSIZE);
	
	// validate the address range
	if (addr2 < addr1)
		return 1;
	
	// Iterate over these pages and print out results
	for (va = (long) addr1; va <= (long) addr2; va += PGSIZE) {

		pte = pgdir_walk(pgdir, (void *) va, false);

		if (show_non_present && (pte == NULL || !(*pte & PTE_P))) {
			cprintf("%08x => XXXXXXXX \t|\t P:0 \t|\t U:X \t|\t W:X\n", (void *) va);
			continue;
		}

		cprintf("%08x => %08x \t|\t P:1 \t|\t U:%d \t|\t W:%d\n", (void *) va,
			PTE_ADDR(*pte),
			*pte & PTE_U ? 1 : 0,
			*pte & PTE_W ? 1 : 0
		);
	}
	
	return 0;
}

/*
 * mon_editmapping edit a mapping of va, including security bits.
 * implicitly creates a PT if the relevant PDE is not present.
 */
int
mon_editmapping(int argc, char **argv, struct Trapframe *tf)
{
	char *va;
	pte_t pte_input, *pte;
	pde_t *pgdir;

	pgdir = (pde_t *) (UVPT | ((UVPT >> PDXSHIFT) << PTXSHIFT));

	if (argc != 3)
		return 1;
	
	va = (char *) ROUNDDOWN((int) strtol(argv[1], NULL, 16), PGSIZE);
	pte_input = (pte_t) strtol(argv[2], NULL, 16);

	pte = pgdir_walk(pgdir, va, 1);

	if (pte == NULL) {
		cprintf("mon_editmapping: Failed to create PT\n");
		return 1;
	}

	*pte = pte_input;
	cprintf("mon_editmapping: updated mapping from page %08x\n", va);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
		const char fmt1[] = "%@r  ebp %@w%08x  %@reip %@w%08x  %@rargs %@w%08x %08x %08x %08x %08x\n";
		const char fmt2[] = "%@g       %s:%d: %.*s+%d\n";
        int32_t * ebp = (int32_t *) read_ebp();
		int32_t * eip, * args;
		struct Eipdebuginfo info;
		
        cprintf("Stack backtrace:\n");
        do {
			// Get eip and parameters list
			eip = (int32_t *) ebp[1];
			args = &ebp[2];
			
			// Retrieve debug info from eip
			debuginfo_eip((uintptr_t)eip, &info);
			
			// Output
            cprintf(fmt1, ebp, eip, args[0], args[1], args[2], args[3], args[4]);
			cprintf(fmt2, info.eip_file, info.eip_line, info.eip_fn_namelen, info.eip_fn_name, (int32_t) eip - (int32_t) info.eip_fn_addr);
        } while((ebp = (int32_t *)*ebp));
	return 0;
}



/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("%@rWe %@buse %@gcolors! %@rIf %@gyou %@bran %@rqemu-nox %@gyou %@wwon't see them probably :(\n");
	cprintf("Type '%@rhelp%@w' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
