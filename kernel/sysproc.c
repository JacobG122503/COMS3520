#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return wait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int n;

  argint(0, &n);
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

uint64
sys_sleep(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//Everything for homework 2 below
uint64 sys_getppid(void) {
  return myproc()->parent->pid; 
}

extern struct spinlock proc_lock;  // Declare the spinlock

int getcpids(int *cpids, int max) {
  struct proc *p = myproc();  // Get the calling process
  int count = 0;
  struct proc *child;

  // Validate the input arguments
  if (cpids == 0 || max <= 0) {
    return -1;  // Invalid arguments
  }

  // Acquire the lock before accessing the process list
  acquire(&proc_lock); // Lock to ensure no race conditions when accessing process list
  
  for (child = p; child < &p[NPROC]; child++) {
    // Check if the child belongs to the calling process
    if (child->parent == p && child->state != UNUSED && child->state != ZOMBIE) {
      // The current process is a child of the calling process
      if (count < max) {
        cpids[count] = child->pid;  // Store child PID in the user-space array
        count++;
      } else {
        break;  // We have filled the array up to max elements
      }
    }
  }
  
  release(&proc_lock); // Release the process lock

  return count;  // Return the number of child processes found
}