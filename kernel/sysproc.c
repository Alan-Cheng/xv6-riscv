#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"
#include "sysinfo.h"

uint64
sys_trace(void)
{
  argint(0, &myproc()->tracemask);
  return 0;
}

uint64
sys_sysinfo(void)
{
  uint64 addr;
  argaddr(0, &addr);
  struct sysinfo info;
  info.freemem = kmemunused();
  info.nproc = allocproccount();
  if (copyout(myproc()->pagetable, myproc()->sz, addr,
              (char *)&info, sizeof(info)) < 0)
    return -1;

  return 0;
}

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  kexit(n);
  return 0; // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return kfork();
}

uint64
sys_wait(void)
{
  uint64 p;
  argaddr(0, &p);
  return kwait(p);
}

// Observe the old break without allocating any page-table pages.
static void
sbrk_pte_debug(char *phase, uint64 addr)
{
  struct proc *p = myproc();
  pte_t *pte = addr < MAXVA ? walk(p->pagetable, addr, 0) : 0;

  if (pte == 0) {
    printk("sbrk %s: pid=%d sz=%lx va=%lx PTE unavailable\n",
           phase, p->pid, p->sz, addr);
  } else {
    printk("sbrk %s: pid=%d sz=%lx va=%lx pte=%lx flags=%lx valid=%d\n",
           phase, p->pid, p->sz, addr, *pte, PTE_FLAGS(*pte),
           (*pte & PTE_V) != 0);
  }
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  // Limit this exercise's output to one-byte growth requests.
  if (n == 1)
    sbrk_pte_debug("before", addr);

  if (t == SBRK_EAGER || n < 0) {
    if (growproc(n) < 0) {
      return -1;
    }
  } else {
    // Lazily allocate memory for this process: increase its memory
    // size but don't allocate memory. If the processes uses the
    // memory, vmfault() will allocate it.
    if (addr + n < addr)
      return -1;
    if (addr + n > TRAPFRAME)
      return -1;
    myproc()->sz += n;
  }
  if (n == 1)
    sbrk_pte_debug("after", addr);
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if (n < 0)
    n = 0;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (killed(myproc())) {
      release(&tickslock);
      return -1;
    }
    sleep_prepare(&ticks);
    release(&tickslock);
    sleep();
    acquire(&tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_kill(void)
{
  int pid;

  argint(0, &pid);
  return kkill(pid);
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
