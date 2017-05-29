
#include "fs.h"
// Return the virtual address of this disk block.
void*
diskaddr(uint32_t blockno)
{
	if (blockno == 0 || (super && blockno >= super->s_nblocks))
		panic("bad block number %08x in diskaddr", blockno);
	return (char*) (DISKMAP + blockno * BLKSIZE);
}

// Is this virtual address mapped?
bool
va_is_mapped(void *va)
{
	return (uvpd[PDX(va)] & PTE_P) && (uvpt[PGNUM(va)] & PTE_P);
}

// Is this virtual address dirty?
bool
va_is_dirty(void *va)
{
	return (uvpt[PGNUM(va)] & PTE_D) != 0;
}

bool
va_is_accessed(void *va)
{
	return uvpt[PGNUM(va)] & PTE_A;
}

// Fault any disk block that is read in to memory by
// loading it from disk.
static void
bc_pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	// Check that the fault was within the block cache region
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("page fault in FS: eip %08x, va %08x, err %04x",
		      utf->utf_eip, addr, utf->utf_err);

	// Sanity check the block number.
	if (super && blockno >= super->s_nblocks)
		panic("reading non-existent block %08x\n", blockno);

	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary. fs/ide.c has code to read
	// the disk.
	//
	// LAB 5: you code here:
	addr = (void *) ROUNDDOWN((uintptr_t) addr, BLKSIZE);

	// LAB 5, Challenge: evict blocks if was called (times % 50 == 0)
	evict_if_necessary();
	
	if ((r = sys_page_alloc(0, addr, PTE_P | PTE_U | PTE_W)) < 0)
		panic("error when allocating page: %e");
	
	if ((r = ide_read(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("cannot read from dist: %e", r);
	
	// Clear the dirty bit for the disk block page since we just read the
	// block from disk
	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);

	// Check that the block we read was allocated. (exercise for
	// the reader: why do we do this *after* reading the block
	// in?)
	if (bitmap && block_is_free(blockno))
		panic("reading free block %08x\n", blockno);
}

// Flush the contents of the block containing VA out to disk if
// necessary, then clear the PTE_D bit using sys_page_map.
// If the block is not in the block cache or is not dirty, does
// nothing.
// Hint: Use va_is_mapped, va_is_dirty, and ide_write.
// Hint: Use the PTE_SYSCALL constant when calling sys_page_map.
// Hint: Don't forget to round addr down.
void
flush_block(void *addr)
{
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;
	
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("flush_block of bad va %08x", addr);

	// LAB 5: Your code here.
	addr = (void *) ROUNDDOWN((uintptr_t) addr, BLKSIZE);
	
	if (!va_is_mapped(addr) || !va_is_dirty(addr))
		return;
	
	if ((r = ide_write(blockno * BLKSECTS, addr, BLKSECTS)) < 0)
		panic("cannot write to dist: %e", r);

	if ((r = sys_page_map(0, addr, 0, addr, uvpt[PGNUM(addr)] & PTE_SYSCALL)) < 0)
		panic("in bc_pgfault, sys_page_map: %e", r);	
}

// Test that the block cache works, by smashing the superblock and
// reading it back.
static void
check_bc(void)
{
	struct Super backup;

	// back up super block
	memmove(&backup, diskaddr(1), sizeof backup);

	// smash it
	strcpy(diskaddr(1), "OOPS!\n");
	flush_block(diskaddr(1));
	assert(va_is_mapped(diskaddr(1)));
	assert(!va_is_dirty(diskaddr(1)));

	// clear it out
	sys_page_unmap(0, diskaddr(1));
	assert(!va_is_mapped(diskaddr(1)));

	// read it back in
	assert(strcmp(diskaddr(1), "OOPS!\n") == 0);

	// fix it
	memmove(diskaddr(1), &backup, sizeof backup);
	flush_block(diskaddr(1));

	cprintf("block cache is good\n");
}

void
bc_init(void)
{
	struct Super super;
	set_pgfault_handler(bc_pgfault);
	check_bc();

	// cache the super block by reading it once
	memmove(&super, diskaddr(1), sizeof super);
}

static short num_inserts =  0;

// lab5 challenge: evicts 10 blocks once every 50 calls
void evict_if_necessary(){
	num_inserts++;
	if(num_inserts % 50){
		num_inserts = 0;
		evict(10, false);
	}
}

/*
 * Lab5 Challenge: evicts @num_blocks, if @hard, evicts accessed too
 */
void evict(int num_blocks, boolean hard){
	int freed_count = 0;
	int res;
	for(int i = 3; i < super->s_nblocks; i++){
		if(freed_count >= num_blocks){
			break;
		}
		// get diskaddr of block i
		void* disk_address = diskaddr(i);

		if(va_is_mapped(disk_address)){
			// the address is mapped:
			// use PTE_A to track usage, as requested in instructions
			if(!va_is_accessed(disk_address)){
				if(!va_is_dirty(disk_address)){
					// block is OK to flush
					freed_count++;
					flush_block(disk_address);
					sys_page_unmap(0, disk_address);
				}
			} else {
				// clear "accessed" bits for future loops (if @freed_count doesn't reach @num_blocks), possibly using sys_page_map
				if(hard && ((res = sys_access_bit_map(disk_address)) < 0))
					panic("in evict, sys_clear_block_access_bit: %e", res);
			}
		} else {
			// @disk_address isn't mapped
			freed_count++;
		}
	}
	if((res = num_blocks - freed_count) > 0) {
		evict(res, true);
	}
}

