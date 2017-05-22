
#include <inc/lib.h>

void
exit(void)
{
// TODO	close_all();
	sys_env_destroy(0);
}

