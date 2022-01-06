#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached 
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

int
sys_uname(void)
{
  char* addr;

  int arg_ret = argptr(0, &addr, 8);
  if(arg_ret != 0){
    return -1;
  }
  addr[0] = 'x';
  addr[1] = 'v';
  addr[2] = '6';
  addr[3] = 0x0;
  return 0;
}

// TODO: Fill in
extern void *GetSharedPage(int i, int len); // For simplicity's sake
void*
sys_GetSharedPage(void)
{
	int key;
	int len;

	if(argint(0, &key) < 0)
		return (void*)-1;
	if(argint(1, &len) < 0)
		return (void*)-1;
	return (void*)(GetSharedPage(key, len));
}

extern int FreeSharedPage(int id);
int
sys_FreeSharedPage(void)
{
	int key;

	if(argint(0, &key) < 0)
		return -1;
	return FreeSharedPage(key);
}