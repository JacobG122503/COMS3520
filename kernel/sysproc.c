#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

#include "syscall.h"

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

uint64
sys_getcpids(void) {
    int *cpids;          // Pointer to the user-space array
    int max;             // Maximum number of child processes to return
    struct proc *p = myproc(); // Current process
    struct proc *child;  // Pointer to iterate through processes
    int count = 0;       // Number of child processes found

    // Fetch arguments from user space
    argaddr(0, (uint64*)&cpids); // Fetch the pointer to the cpids array
    argint(1, &max);             // Fetch the value of max

    // Iterate through all processes to find children
    for (int i = 0; i < NPROC; i++) {
        child = &proc[i];
        if (child->parent == p) { // Check if the process is a child of the current process
            if (count < max) {    // Ensure we don't exceed the user-provided array size
                // Copy the child's PID to the user-space array
                if (copyout(p->pagetable, (uint64)(cpids + count), (char*)&child->pid, sizeof(int)) < 0) {
                    return -1; // Copyout failed
                }
                count++;
            }
        }
    }

    return count; // Return the number of child processes found
}