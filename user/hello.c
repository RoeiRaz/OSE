// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);
	void* addr =(void*) 0xd00;

/*
	cprintf("I FUEL %d\n", sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE), (PTE_P | PTE_U | PTE_W)));
	cprintf("II MOVEIT %d\n", sys_access_bit_map(ROUNDDOWN(addr, PGSIZE)));
*/
}
