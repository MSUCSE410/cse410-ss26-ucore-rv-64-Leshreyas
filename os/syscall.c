#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "limits.h"
#include "trap.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!\n");
	return fork();
}

uint64 sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	char filename[200];
	struct proc *p = curr_proc();
    copyinstr(p->pagetable, filename, va, 200);
    
    // int id = get_id_by_name(filename);
	// TODO: your job is to complete the sys call
	int id = get_id_by_name(filename);
	if (id < 0)
		return -1;
	struct proc *np;

	// Allocate process.
	if ((np = allocproc()) == 0) {
		panic("allocproc\n");
	}
	np->trapframe->a0 = 0;
	np->parent = p;
	np->state = RUNNABLE;
	uvmunmap(np->pagetable, 0, np->max_page, 1);
	np->max_page = 0;
	loader(id, np);
	add_task(np);
	return np->pid;
	// return -1;

}

uint64 sys_set_priority(long long prio){
    // TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	if (prio < 2 || prio > LLONG_MAX) {
		return -1;
	}
	p->priority = prio;
	p->pass = 65536 / p->priority;
    return prio;
}

uint64 sys_task_info(struct TaskInfo *info)
{
	struct proc *p = curr_proc();
	struct TaskInfo tsk;
	tsk.status = Running;
    tsk.time = ((get_cycle() - p->info.time)/ (CPU_FREQ/1000));
	memmove(tsk.syscall_times, p->info.syscall_times, sizeof(tsk.syscall_times));
	if (copyout(curr_proc()->pagetable, (uint64) info, (char *)&tsk, sizeof(tsk)) < 0)
	{
		return -1;
	}
    
    
    return 0;
}

uint64 sys_mmap(uint64 start, uint64 len, int port, int flag, int fd)
{
	if (len == 0) return 0;
	if (start % PAGE_SIZE != 0) return -1;
    if ((port & ~0x7) != 0) return -1;              
    if ((port & 0x7) == 0) return -1;
	struct proc *p = curr_proc();
	len = PGROUNDUP(len);
	uint64 flags = 0;
	int mask =  1 << 0;
    int masked_n = port & mask;
    int thebit = masked_n >> 0;
	flags |= PTE_U;
	if (thebit == 1)
	{
		flags |= PTE_R;
	}
	mask =  1 << 1;
    masked_n = port & mask;
    thebit = masked_n >> 1;
	if (thebit == 1)
	{
		flags |= PTE_W;
	}
	mask =  1 << 2;
    masked_n = port & mask;
    thebit = masked_n >> 2;
	if (thebit == 1)
	{
		flags |= PTE_X;
	}
	for (uint64 va = start; va < start + len; va += PAGE_SIZE) 
	{
		if (walkaddr(p->pagetable, va) == 0)
		{
		void *pa = kalloc();
		if (pa == 0) return -1;
        if (mappages(p->pagetable, va, PAGE_SIZE, (uint64)pa, flags) != 0) return -1;
		} 
		else
		{
			return -1;
		}
	}
	return 0;
}

uint64 sys_munmap(uint64 start, uint64 len)
{
	struct proc *p = curr_proc();
	if (start % PAGE_SIZE != 0) return -1;
	len = PGROUNDUP(len);
	uint64 pte;
	for (uint64 va = start; va < start + len; va += PAGE_SIZE)
	{
		pte = walkaddr(p->pagetable, va);
		if (pte == 0) return -1;
		uvmunmap(p->pagetable, va, 1, 1);
	} 
	return 0;
}

// TODO: add support for mmap and munmap syscall.
// hint: read through docstrings in vm.c. Watching CH4 video may also help.
// Note the return value and PTE flags (especially U,X,W,R)
/*
* LAB1: you may need to define sys_task_info here
*/

extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	/*
	* LAB1: you may need to update syscall counter for task info here
	*/
	curr_proc()->info.syscall_times[id] += 1;
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	/*
	* LAB1: you may need to add SYS_taskinfo case here
	*/
	case SYS_task_info:
		ret = sys_task_info((struct TaskInfo *)args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap(args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap(args[0], args[1]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
