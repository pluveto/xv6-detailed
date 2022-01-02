// Memory layout

#define EXTMEM  0x100000            // Start of extended memory
#define PHYSTOP 0xE000000           // Top physical memory
#define DEVSPACE 0xFE000000         // Other devices are at high addresses

// Key addresses for address space layout (see kmap in vm.c for layout)
// 进程的用户内存从 0 开始，最多能够增长到 KERNBASE, 这使得一个进程最多只能使用 2GB 的内存
#define KERNBASE 0x80000000         // First kernel virtual address
#define KERNLINK (KERNBASE+EXTMEM)  // Address where kernel is linked


#define V2P(a) (((uint) (a)) - KERNBASE) // 将内核虚拟地址转换为物理地址

#define P2V(a) ((void *)(((char *) (a)) + KERNBASE))// 将内核物理地址转换为虚拟地址

#define V2P_WO(x) ((x) - KERNBASE)    // same as V2P, but without casts
#define P2V_WO(x) ((x) + KERNBASE)    // same as P2V, but without casts
