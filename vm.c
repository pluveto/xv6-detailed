#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()

// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs(页表项) for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
// mappages（1679）做的工作是在页表中建立一段虚拟内存到一段物理内存的映射。
// 它是在页的级别，即一页一页地建立映射的。
// 对于每一个待映射虚拟地址，mappages 调用 walkpgdir 来找到该地址对应的 PTE 地址。
// 然后初始化该 PTE 以保存对应物理页号、许可级别（PTE_W 和/或 PTE_U）
// 以及 PTE_P 位来标记该 PTE 是否是有效的（1691）。
static int
mappages(pde_t *pgdir, void *va /* 虚拟地址 */ , uint size, uint pa /* 物理地址 */, int perm)
{
  char *a, *last;
  pte_t *pte;

  // 首页基地址
  a = (char*)PGROUNDDOWN((uint)va);
  // 末页基地址
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);

  // 一页页创建映射
  for(;;){
    // 获取本页的 PTE
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    // 设置 PTE 的物理地址、权限等信息。（本函数的核心功能）
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
// 内核需要的映射。包括内核的指令和数据，PHYSTOP 以下的物理内存，以及 I/O 设备所占的内存
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
  // KERNBASE 以下的留给用户程序
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
// 申请一段内存作为页目录，并且在其中初始化内核的页表
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    // 对内核页进行映射
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
// TLB 只能使用虚拟地址来做 tag，因此存在歧义问题。
// 在向CR3寄存器写入值时 可以让处理器自动刷新相对于非全局页的TLB表项；
// lcr3(V2P(kpgdir)) 将 kpgdir （内核页目录）地址加载到 CR3 寄存器，从而实现将当前页表切换为内核的页表
void
switchkvm(void)
{  
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// 切换到进程 p 的页表和 TSS
// Switch TSS and h/w page table to correspond to process p.
// TSS是什么？TSS 的主要作用就是保存任务的快照，也就是CPU 执行该任务时，寄存器当时的瞬时值。
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
// init is the code to be loaded
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.

#define MAX_REGION_SIZE 32
typedef char bool;
struct shm_region {
	bool valid;
	int rc; // 引用计数
	int len;
	uint physical_pages[MAX_REGION_SIZE];
};

struct shm_region regions[32];

// Map region <key> into process <p> at virtual addres <addr>
void map_shm_region(int key, struct proc *p, void *addr) {
	for (int k = 0; k < regions[key].len; k++) {
		mappages(p->pgdir, (void*)(addr + (k*PGSIZE)), PGSIZE, regions[key].physical_pages[k], PTE_W | PTE_U);
	}
}

// Syscall which maps and if necessary allocates pages into the calling process' memory space, allowing for shared memory pages
void *
GetSharedPage(int key, int len)
{
  if(key < 0 || key > 32)
    return (void*)0;
	// Allocate pages in the appropriate regions' pysical pages
	if(!regions[key].valid) {
		for(int j = 0; j < len; j++) {
			void* newpage = kalloc(); // Get new page
      memset(newpage, 0, PGSIZE); // Zero out pallocprocage
			regions[key].physical_pages[j] = V2P(newpage); // Save new page
		}
		regions[key].valid = 1;
		regions[key].len = len;
		regions[key].rc = 0;
	} else {
		if(regions[key].len != len)
			return (void*)0;
	}
	regions[key].rc += 1;

	// Find the index in the process
	struct proc *p = myproc();
	int shind = -1;
	for (int x = 0; x < 32; x++) {
		if (p->shm[x].key == -1) {
			shind = x;
			break;
		}
	}
	if (shind == -1)
		return (void*)0;

	// Get the lowest virtual address space currently allocated
	void *va = (void*)KERNBASE-PGSIZE;
	for (int x = 0; x < 32; x++) {
		if (p->shm[x].key != -1 && (uint)(va) > (uint)(p->shm[x].va)) {
			va = p->shm[x].va;
		}
	}

	// Get va of new mapped pages
	va = (void*)va - (len*PGSIZE);
	p->shm[shind].va = va;
	p->shm[shind].key = key;

	// Map them in memory
	map_shm_region(key, p, va);

	return va;
}

// Copy the shared memory regions of process p into process np
int copy_shared_regions(struct proc *p, struct proc *np) {
	for (int i = 0; i < 32; i++) {
		if (p->shm[i].key != -1) {
			np->shm[i] = p->shm[i];
			// map into the new forked proc
			int key = np->shm[i].key;
			regions[key].rc++;
			map_shm_region(key, np, np->shm[i].va);
		}
	}
	return 0;
}

// Syscall for handling freeing of shared pages.
int
FreeSharedPage(int key)
{
	// Clear shared memory data structure
	struct proc *p = myproc();
	void *va = 0;
	for (int i = 0; i < 32; i++) {
		if (p->shm[i].key == key) {
			va = p->shm[i].va;
			p->shm[i].key = -1;
			p->shm[i].va = 0;
			break;
		}
	}
	if(va == 0)
		return -1;

	// Clear page table entries for all pages in the process
	struct shm_region* reg = &regions[key];
	for(int i = 0; i < reg->len; i++) {
		pte_t* pte = walkpgdir(p->pgdir, (char*)va + i*PGSIZE, 0);
		if(pte == 0) {
			return -1;
		}
		*pte = 0;
	}

	// Decrease the refcount, freeing if unused.
	reg->rc--;
	if(reg->rc == 0) {
		regions[key].valid = 0;
		regions[key].rc = 0;
		for(int i = 0; i < regions[key].len; i++)
			kfree(P2V(regions[key].physical_pages[i]));
		regions[key].len = 0;
	}

	return 0;
}

// Check if given address is associated with a shared memory page
int is_shm_pa(uint v) {
  for (int i = 0; i < 32; i++) {
    if (!regions[i].valid) continue;
    for (int r = 0; r < regions[i].len; r++) {
      if (v == (uint)regions[i].physical_pages[r]) {
        return 1;
      }
    }
  }
	return 0;
}

// Free any pages which fail is_shm_pa (are not shared)
void possibly_free_physical_page(void *v) {
  if (!is_shm_pa(V2P(v))) kfree(v);
}