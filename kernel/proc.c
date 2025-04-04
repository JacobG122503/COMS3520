#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include <limits.h>

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

//CFS scheduler main function
void cfs_scheduler(struct cpu *c);
//Start CFS with given params
int startcfs(int quantum, int weight, int decay);
//Stop CFS
int stopcfs(void);
//Get process runtime info
void get_proc_runtime(struct proc *p, int *actual, int *virtual);

struct cfs_proc {
  struct proc *p;
  int vruntime;
};

int cfs_quantum;
int cfs_weight;
int cfs_decay;
int cfs_count = 0;
struct cfs_proc cfs_queue[NPROC];

extern char trampoline[];

struct spinlock wait_lock;

void proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

void procinit(void) {
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  
  for(int i = 0; i < NPROC; i++) {
    cfs_queue[i].p = 0;
    cfs_queue[i].vruntime = 0;
  }
  cfs_count = 0;

  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

int cpuid() {
  int id = r_tp();
  return id;
}

struct cpu* mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

struct proc* myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int allocpid() {
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

static struct proc* allocproc(void) {
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  p->runtime = 0;
  p->vruntime = 0;

  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

static void freeproc(struct proc *p) {
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  p->nice = 0;
  p->runtime = 0;
  p->vruntime = 0;
}

pagetable_t proc_pagetable(struct proc *p) {
  pagetable_t pagetable;

  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

void proc_freepagetable(pagetable_t pagetable, uint64 sz) {
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

void userinit(void) {
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  p->trapframe->epc = 0;
  p->trapframe->sp = PGSIZE;

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

int growproc(int n) {
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

int fork(void) {
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  if((np = allocproc()) == 0){
    return -1;
  }

  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  *(np->trapframe) = *(p->trapframe);

  np->trapframe->a0 = 0;

  acquire(&p->lock);
  np->nice = p->nice;
  np->vruntime = p->vruntime;
  np->runtime = 0;
  release(&p->lock);

  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

void reparent(struct proc *p) {
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

void exit(int status) {
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  reparent(p);

  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  sched();
  panic("zombie exit");
}

int wait(uint64 addr) {
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    sleep(p, &wait_lock);
  }
}

int cfs;
int cfs_sched_latency;     
int cfs_max_timeslice;
int cfs_min_timeslice;

int nice_to_weight[40] = {
  88761, 71755, 56483, 46273, 36291,
  29154, 23254, 18705, 14949, 11916,
  9548, 7620, 6100, 4904, 3906,
  3121, 2501, 1991, 1586, 1277,
  1024, 820, 655, 526, 423,
  335, 272, 215, 172, 137,
  110, 87, 70, 56, 45,
  36, 29, 23, 18, 15,
};

struct proc *cfs_current_proc = 0;
int cfs_proc_timeslice_len = 0;
int cfs_proc_timeslice_left = 0;

//Calculate total weight of all runnable processes
int weight_sum() {
  int sum = 0;
  struct proc *p;
  
  for(int i = 0; i < NPROC; i++) {
    p = &proc[i];
    if (!p) continue;
    
    if (p->lock.locked) {
      continue; 
  }
    acquire(&p->lock);
    if(p->state == RUNNABLE) {
      int nice_index = p->nice + 20;
      if(nice_index >= 0 && nice_index < 40) {
        sum += nice_to_weight[nice_index];
      } else {
        sum += nice_to_weight[20];
      }
    }
    release(&p->lock);
  }
  return sum;
}

//Find process with smallest vruntime
struct proc* shortest_runtime_proc() {
  struct proc *p;
  struct proc *min_proc = 0;
  int min_vruntime = INT_MAX;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == RUNNABLE && p->vruntime < min_vruntime) {
      if(min_proc) {
        if(!holding(&min_proc->lock)) panic("missing lock");
        release(&min_proc->lock);
      }
      min_proc = p;
      min_vruntime = p->vruntime;
    } else {
      release(&p->lock);
    }
  }
  return min_proc;
}

//Main CFS scheduling function
void cfs_scheduler(struct cpu *c) {
  c->proc = 0;
  
  if (cfs_current_proc) {
    cfs_proc_timeslice_left--;
    
    if (cfs_proc_timeslice_left > 0) {
      acquire(&cfs_current_proc->lock);
      if (cfs_current_proc->state == RUNNABLE) {
        c->proc = cfs_current_proc;
      }
      release(&cfs_current_proc->lock);
    } else {
      acquire(&cfs_current_proc->lock);
      if (cfs_current_proc->state == RUNNABLE) {
        int weight = nice_to_weight[cfs_current_proc->nice + 20];
        int inc = (cfs_proc_timeslice_len - cfs_proc_timeslice_left) * 1024 / weight;
        inc = (inc < 1) ? 1 : inc;

        cfs_current_proc->vruntime += inc;
        cfs_current_proc->runtime += (cfs_proc_timeslice_len - cfs_proc_timeslice_left);  
        
        printf("[DEBUG CFS] Process %d used %d ticks of its assigned timeslice (totally %d ticks) and swapped out!\n", 
               cfs_current_proc->pid, cfs_proc_timeslice_len - cfs_proc_timeslice_left, cfs_current_proc->runtime);
      }
      release(&cfs_current_proc->lock);
    }
  }
  if (c->proc == 0) {
    struct proc *p = shortest_runtime_proc();
    if (p) {
      int weight = nice_to_weight[p->nice + 20];
      int sum = weight_sum();
      if (sum == 0) sum = 1;
      
      cfs_proc_timeslice_len = (cfs_sched_latency * weight) / sum;
      if (cfs_proc_timeslice_len < cfs_min_timeslice) 
        cfs_proc_timeslice_len = cfs_min_timeslice;
      if (cfs_proc_timeslice_len > cfs_max_timeslice) 
        cfs_proc_timeslice_len = cfs_max_timeslice;
      
      cfs_proc_timeslice_left = cfs_proc_timeslice_len;
      cfs_current_proc = p;
      c->proc = p;
      
      printf("[DEBUG CFS] Process %d scheduled to run for a timeslice of %d ticks next!\n", 
             p->pid, cfs_proc_timeslice_len);
      
      p->state = RUNNING;
      swtch(&c->context, &p->context);   
      
      release(&p->lock);
    }
  }
}

void scheduler(void) {
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;) {
    intr_on();
    
    if (cfs) {
      cfs_scheduler(c);
    } else {
      struct proc *p;
      for(p = proc; p < &proc[NPROC]; p++) {
        acquire(&p->lock);
        if(p->state == RUNNABLE) {
          p->state = RUNNING;
          c->proc = p;
          swtch(&c->context, &p->context);
          c->proc = 0;
        }
        release(&p->lock);
      }
    }
  }
}

void sched(void) {
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

void yield(void) {
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

void forkret(void) {
  static int first = 1;

  release(&myproc()->lock);

  if (first) {
    first = 0;
    fsinit(ROOTDEV);
  }

  usertrapret();
}

void sleep(void *chan, struct spinlock *lk) {
  struct proc *p = myproc();
  
  acquire(&p->lock);
  release(lk);

  p->chan = chan;
  p->state = SLEEPING;

  sched();

  p->chan = 0;

  release(&p->lock);
  acquire(lk);
}

void wakeup(void *chan) {
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

int kill(int pid) {
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void setkilled(struct proc *p) {
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int killed(struct proc *p) {
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

int either_copyout(int user_dst, uint64 dst, void *src, uint64 len) {
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

int either_copyin(void *dst, int user_src, uint64 src, uint64 len) {
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

void procdump(void) {
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}

//Set process nice value (affects CFS scheduling)
int nice(int value) {
  struct proc *p = myproc();

  if (value < -20 || value > 19) {
      return p->nice;
  }

  acquire(&p->lock);
  p->nice = value;
  release(&p->lock);

  return p->nice;
}

//Initialize CFS scheduler with parameters
int startcfs(int quantum, int weight, int decay) {
  cfs = 1;
  cfs_sched_latency = quantum;
  cfs_max_timeslice = weight;
  cfs_min_timeslice = decay;
  return 0;
}

//Stop CFS scheduler
int stopcfs(void) {
  cfs = 0;
  return 0;
}

int getruntime(int *pid, int *vruntime) {
  struct proc *p = myproc(); 
  if (!p) return -1;
  int actual, virtual;
 
  get_proc_runtime(p, &actual, &virtual);
 
  printf("[SUMMARY] process (pid=%d): finishes comutation. During CFS: actual runtime = %d; virtual runtime = %d\n", p->pid, actual, virtual); 
 
  return 0;
 }
 

//Helper to get process runtime stats
void get_proc_runtime(struct proc *p, int *actual, int *virtual) {
  acquire(&p->lock);
  *actual = p->runtime;
  *virtual = p->vruntime;
  release(&p->lock);
}