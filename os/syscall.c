#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "limits.h"
#include "file.h"
#include "fs.h"
#include "stddef.h"

#define DIR 0x040000
#define FILE 0x100000


uint64 console_write(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	tracef("write size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return len;
}

uint64 console_read(uint64 va, uint64 len)
{
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	tracef("read size = %d", len);
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

uint64 sys_write(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_write(va, len);
	case FD_INODE:
		return inodewrite(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d\n", fd);
		return -1;
	}
	switch (f->type) {
	case FD_STDIO:
		return console_read(va, len);
	case FD_INODE:
		return inoderead(f, va, len);
	default:
		panic("unknown file type %d\n", f->type);
	}
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
	debugf("fork!");
	return fork();
}

static inline uint64 fetchaddr(pagetable_t pagetable, uint64 va)
{
	uint64 *addr = (uint64 *)useraddr(pagetable, va);
	return *addr;
}

uint64 sys_exec(uint64 path, uint64 uargv)
{
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	return exec(name, (char **)argv);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// // TODO: your job is to complete the sys call
	// char filename[200];
	// struct proc *p = curr_proc();
    // copyinstr(p->pagetable, filename, va, 200);
    
    // // int id = get_id_by_name(filename);
	// // TODO: your job is to complete the sys call
	// int id = get_id_by_name(filename);
	// if (id < 0)
	// 	return -1;
	// struct proc *np;

	// // Allocate process.
	// if ((np = allocproc()) == 0) {
	// 	panic("allocproc\n");
	// }
	// np->trapframe->a0 = 0;
	// np->parent = p;
	// np->state = RUNNABLE;
	// uvmunmap(np->pagetable, 0, np->max_page, 1);
	// np->max_page = 0;
	// loader(id, np);
	// add_task(np);
	// return np->pid;
	return -1;
}

uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
{
	struct proc *p = curr_proc();
	char path[200];
	copyinstr(p->pagetable, path, va, 200);
	return fileopen(path, omode);
}

uint64 sys_close(int fd)
{
	if (fd < 0 || fd > FD_BUFFER_SIZE)
		return -1;
	struct proc *p = curr_proc();
	struct file *f = p->files[fd];
	if (f == NULL) {
		errorf("invalid fd %d", fd);
		return -1;
	}
	fileclose(f);
	p->files[fd] = 0;
	return 0;
}

int sys_fstat(int fd,uint64 stat){
	//TODO: your job is to complete the syscall
	if (fd < 0 || fd > FD_BUFFER_SIZE) {
		return -1;
	}
	struct file *f = curr_proc()->files[fd];
	if (f == NULL || f->type != FD_INODE) {
		return -1;
	}
	Stat kstat;
	ivalid(f->ip);
	kstat.dev = f->ip->dev;
	kstat.ino = f->ip->inum;
	kstat.nlink = f->ip->nlink;
	if (f->ip->type == T_FILE)
	{
		kstat.mode = FILE;
	}
	else
	{
		kstat.mode = DIR;
	}
	copyout(curr_proc()->pagetable, stat, (char *)&kstat, sizeof(Stat));
	return 0;
}

int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags){
	struct inode *dp, *old_ip;
	dp = root_dir();
	ivalid(dp);
	char oldpath_str[200];
	char newpath_str[200];
	if (copyinstr(curr_proc()->pagetable, oldpath_str, oldpath, sizeof(oldpath_str)) < 0) {
		iput(dp);
		return -1;
	}
	if (copyinstr(curr_proc()->pagetable, newpath_str, newpath, sizeof(newpath_str)) < 0) {
		iput(dp);
		return -1;
	}
	if (oldpath == newpath) { return -1;}
	old_ip = dirlookup(dp, oldpath_str, 0);
	// ivalid(old_ip);
	if (old_ip == 0) { return -1;}
	old_ip->nlink += 1;
	iupdate(old_ip);
	dirlink(dp, newpath_str, old_ip->inum);
	printf("Reference count: %d\n", old_ip->nlink);
	return 0;
}

int sys_unlinkat(int dirfd, uint64 name, uint64 flags){
	struct inode *dp, *old_ip;
	dp = root_dir();
	ivalid(dp);
	char name_str[200];
	if (copyinstr(curr_proc()->pagetable, name_str, name, sizeof(name_str)) < 0) {
		iput(dp);
		return -1;
	}
	old_ip = dirlookup(dp, name_str, 0);
	if (old_ip == 0) {
		return -1;
	}
	ivalid(old_ip);
	if (dirunlink(dp, name_str, old_ip->inum) < 0) {
		return -1;
	}

	old_ip->nlink -= 1;
	debugf("Reference count: %d\n", old_ip->nlink);
	iput(old_ip);
	iupdate(old_ip);
	return 0;
}
	

uint64 sys_set_priority(long long prio){
    // TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	if (prio < 2 || prio > INT_MAX) {
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
	case SYS_openat:
		ret = sys_openat(args[0], args[1], args[2]);
		break;
	case SYS_close:
		ret = sys_close(args[0]);
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
		ret = sys_exec(args[0], args[1]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_fstat:
	    ret = sys_fstat(args[0],args[1]);
		break;
	case SYS_linkat:
	    ret = sys_linkat(args[0],args[1],args[2],args[3],args[4]);
		break;
	case SYS_unlinkat:
	    ret = sys_unlinkat(args[0],args[1],args[2]);
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
