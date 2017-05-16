// hello, world
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	cprintf("hello, world\n");
	cprintf("i am environment %08x\n", thisenv->env_id);

	int pid;

	for (int i = 0; i < 20; i++) {
		if ((pid = priority_fork(20-i)) == 0) {
			cprintf("start----priority %d\n", 20 - i);
			for (int j = 0; j < 5; j++) {
				cprintf("yield----priority %d\n", 20 - i);
				sys_yield();
			}
			break;
		}
	}

}
