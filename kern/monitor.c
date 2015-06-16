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
	{ "backtrace", "Display the stack backtrace", mon_backtrace},
	{ "showmappings", "Show the mappings info", mon_showmappings},
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

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	// Your code here.
	uint32_t ebp, eip;
	uint32_t arg1, arg2, arg3, arg4, arg5;
	struct Eipdebuginfo info;

	cprintf("Stack backtrace:\n");

	for (ebp = read_ebp(); ebp != 0; ebp = *(uint32_t *)ebp)
	{
		eip = *(uint32_t *)(ebp + 4);
		arg1 = *(uint32_t *)(ebp + 8);
		arg2 = *(uint32_t *)(ebp + 12);
		arg3 = *(uint32_t *)(ebp + 16);
		arg4 = *(uint32_t *)(ebp + 20);
		arg5 = *(uint32_t *)(ebp + 24);
			
		cprintf("  ebp %08x  eip %08x  args %08x %08x %08x %08x %08x\n", ebp, eip, arg1, arg2, arg3, arg4, arg5);
		
		if (!debuginfo_eip(eip, &info))
		{
			cprintf("         %s:%d:", info.eip_file, info.eip_line);
			cprintf("  %.*s", info.eip_fn_namelen, info.eip_fn_name);
			cprintf("+%d\n", eip - info.eip_fn_addr);
		}
		else
			cprintf("not found\n");
	}
		return 0;
}

int 
mon_showmappings(int argc, char **argv, struct Trapframe *tf)
{
	uintptr_t va_start, va_end, va;
	pte_t *pte;
	int i, npages;

	if (argc != 3)
		cprintf("error: showmappings needs 2 parameters\n");
	else
	{
		va_start = (uintptr_t)strtol(argv[1], NULL, 16);
		va_end = (uintptr_t)strtol(argv[2], NULL ,16);
		
		if (va_end < va_start)
			cprintf("error: va_end < va_start\n");
		else
		{	
			npages = ROUNDUP(va_end - va_start + 1, PGSIZE);
			npages = npages >> PGSHIFT;

			for (i = 0, va = va_start; i < npages; i++, va += PGSIZE)
			{	
				pte = pgdir_walk(kern_pgdir,(void *) va, 0);

				if (pte && (*pte & PTE_P))
					cprintf("vapaddr:0x%x --> paddr:0x%x, p = %d, w = %d, u = %d\n",
							va, PTE_ADDR(*pte), *pte & PTE_P,
							(*pte & PTE_W) >> 1, (*pte & PTE_U) >> 2);
				else
					cprintf("vaddr:0x%x is not mapped\n", va);
			}
		}	
	}

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
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
