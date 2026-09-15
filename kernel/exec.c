#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

// Bound interpreter chains, including scripts that name themselves.
#define MAXINTERP 4
struct scriptargs {
  char line[MAXINTERP][MAXPATH];
  char path[MAXINTERP][MAXPATH];
  char *argv[MAXARG];
};

// map ELF permissions to PTE permission bits.
int
flags2perm(int flags)
{
  int perm = 0;
  if (flags & 0x1)
    perm = PTE_X;
  if (flags & 0x2)
    perm |= PTE_W;
  return perm;
}

//
// the implementation of the exec() system call
//
int
kexec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();
  struct scriptargs *script = 0;
  int depth = 0;

again:
  begin_op();

  // Open the executable file.
  if ((ip = namei(path)) == 0) {
    end_op();
    goto bad;
  }
  ilock(ip);

  // Check for #! before requiring a complete ELF header: scripts can be tiny.
  int n = readi(ip, 0, (uint64)&elf, 0, sizeof(elf));
  if (n >= 2 && ((char *)&elf)[0] == '#' && ((char *)&elf)[1] == '!') {
    if (depth == MAXINTERP)
      goto bad;
    if (script == 0) {
      script = (struct scriptargs *)kalloc();
      if (script == 0)
        goto bad;
    }
    char *line = script->line[depth];
    n = readi(ip, 0, (uint64)line, 0, MAXPATH);
    if (n < 2)
      goto bad;
    for (i = 2; i < n && line[i] != '\n'; i++) {
      if (line[i] == 0)
        goto bad;
    }
    if (i == MAXPATH)
      goto bad; // Do not silently truncate an interpreter line.
    line[i] = 0;

    char *interp = line + 2;
    while (*interp == ' ' || *interp == '\t')
      interp++;
    s = interp;
    while (*s && *s != ' ' && *s != '\t')
      s++;
    char *option = s;
    if (*s) {
      *s++ = 0;
      while (*s == ' ' || *s == '\t')
        s++;
      option = s;
      while (*s)
        s++;
      while (s > option && (s[-1] == ' ' || s[-1] == '\t'))
        *--s = 0;
    }
    if (*interp == 0)
      goto bad;

    // argv becomes: interpreter, optional argument, script path, argv[1...].
    // Preserve strings until they have been copied onto the new user stack.
    int count = 0;
    while (count < MAXARG && argv[count])
      count++;
    int skip = count > 0 ? 1 : 0;
    int prefix = *option ? 3 : 2;
    if (prefix + count - skip >= MAXARG || strlen(path) >= MAXPATH)
      goto bad;
    safestrcpy(script->path[depth], path, MAXPATH);
    for (i = count - 1; i >= skip; i--)
      script->argv[prefix + i - skip] = argv[i];
    script->argv[prefix + count - skip] = 0;
    script->argv[0] = interp;
    if (*option)
      script->argv[1] = option;
    script->argv[prefix - 1] = script->path[depth];
    argv = script->argv;
    path = interp;
    depth++;
    // Release the script before opening its interpreter (which may be itself).
    iunlockput(ip);
    end_op();
    ip = 0;
    goto again;
  }
  if (n != sizeof(elf))
    goto bad;

  // Is this really an ELF file?
  if (elf.magic != ELF_MAGIC)
    goto bad;

  if ((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // Load program into memory.
  for (i = 0, off = elf.phoff; i < elf.phnum; i++, off += sizeof(ph)) {
    if (readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if (ph.type != ELF_PROG_LOAD)
      continue;
    if (ph.memsz < ph.filesz)
      goto bad;
    if (ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if (ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if ((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz,
                        flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if (loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // Allocate some pages at the next page boundary.
  // Make the first inaccessible as a stack guard.
  // Use the rest as the user stack.
  sz = PGROUNDUP(sz);
  uint64 sz1;
  if ((sz1 = uvmalloc(pagetable, sz, sz + (USERSTACK + 1) * PGSIZE, PTE_W)) ==
      0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz - (USERSTACK + 1) * PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK * PGSIZE;

  // Copy argument strings into new stack, remember their
  // addresses in ustack[].
  for (argc = 0; argv[argc]; argc++) {
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // riscv sp must be 16-byte aligned
    if (sp < stackbase)
      goto bad;
    if (copyout(pagetable, sz, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // push a copy of ustack[], the array of argv[] pointers.
  sp -= (argc + 1) * sizeof(uint64);
  sp -= sp % 16;
  if (sp < stackbase)
    goto bad;
  if (copyout(pagetable, sz, sp, (char *)ustack, (argc + 1) * sizeof(uint64)) <
      0)
    goto bad;

  // a0 and a1 contain arguments to user main(argc, argv)
  // argc is returned via the system call return
  // value, which goes in a0.
  p->trapframe->a1 = sp;

  // Save program name for debugging.
  for (last = s = path; *s; s++)
    if (*s == '/')
      last = s + 1;
  safestrcpy(p->name, last, sizeof(p->name));

  // Commit to the user image.
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry; // initial program counter = ulib.c:start()
  p->trapframe->sp = sp;         // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);
  if (script)
    kfree((void *)script);

  return argc; // this ends up in a0, the first argument to main(argc, argv)

bad:
  if (pagetable)
    proc_freepagetable(pagetable, sz);
  if (ip) {
    iunlockput(ip);
    end_op();
  }
  if (script)
    kfree((void *)script);
  return -1;
}

// Load an ELF program segment into pagetable at virtual address va.
// va must be page-aligned
// and the pages from va to va+sz must already be mapped.
// Returns 0 on success, -1 on failure.
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset,
        uint sz)
{
  uint i, n;
  uint64 pa;

  for (i = 0; i < sz; i += PGSIZE) {
    pa = walkaddr(pagetable, va + i);
    if (pa == 0)
      panic("loadseg: address should exist");
    if (sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if (readi(ip, 0, (uint64)pa, offset + i, n) != n)
      return -1;
  }

  return 0;
}
