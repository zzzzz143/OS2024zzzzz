#                         操作系统Lab4

<h3 align = "center">实验名称：进程管理</h3>
<h4 align = "center">成员：张政泽  马有朝  蔡鸿</h4>

## 练习1 分配并初始化一个进程控制块

### 1. 完成alloc_proc函数

alloc_proc函数主要是分配并且初始化一个PCB用于管理新进程的信息。proc_struct 结构的信息如下：

```C
struct proc_struct {
    enum proc_state state;                      // Process state
    int pid;                                    // Process ID
    int runs;                                   // the running times of Proces
    uintptr_t kstack;                           // Process kernel stack
    volatile bool need_resched;                 // bool value: need to be rescheduled to release CPU?
    struct proc_struct *parent;                 // the parent process
    struct mm_struct *mm;                       // Process's memory management field
    struct context context;                     // Switch here to run process
    struct trapframe *tf;                       // Trap frame for current interrupt
    uintptr_t cr3;                              // CR3 register: the base addr of Page Directroy Table(PDT)
    uint32_t flags;                             // Process flag
    char name[PROC_NAME_LEN + 1];               // Process name
    list_entry_t list_link;                     // Process link list 
    list_entry_t hash_link;                     // Process hash list
};
```

在alloc_proc中我们对每个变量都进行初始化操作，代码如下：

```C
static struct proc_struct *
alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        proc->state = PROC_UNINIT;              // 进程状态设置为未初始化状态
        proc->pid = -1;                         // 未初始化的进程，其pid为-1
        proc->runs = 0;                         // 运行时间设为零
        proc->kstack = 0;                       // 内核栈地址,因为还没有执行，也没有被重定位，默认地址都是从0开始的
        proc->need_resched = 0;                 // 不需要重调度，设置为假
        proc->parent = NULL;                    // 父进程为空
        proc->mm = NULL;                        // 虚拟内存为空
        memset(&(proc->context), 0, sizeof(struct context));// 初始化上下文
        proc->tf = NULL;                        // 中断帧指针为空
        proc->cr3 = boot_cr3;                   // 页目录为内核页目录表的基址
        proc->flags = 0;                        // 标志位为0
        memset(proc->name, 0, PROC_NAME_LEN);   // 进程名为0

    }
    return proc;
}
```

state设置为未初始化状态；
由于刚创建进程，pid设置为-1；
进程运行时间run初始化为0；
内核栈地址kstack默认从0开始；
need_resched是一个用于判断当前进程是否需要被调度的bool类型变量，为1则需要进行调度。初始化为0，表示不需要调度；
父进程parent设置为空；
内存空间初始化为空；
上下文结构体context初始化为0；
中断帧指针tf设置为空；
页目录cr3设置为为内核页目录表的基址boot_cr3；
标志位flags设置为0；
进程名name初始化为0；

### 2.请说明proc_struct中struct context context和struct trapframe *tf成员变量含义和在本实验中的作用是啥？

* context：context中保存了进程执行的上下文，也就是几个关键的寄存器（包含了ra，sp，s0~s11共14个寄存器）的值。这些寄存器的值用于在进程切换中还原之前进程的运行状态。进程切换的详细过程的实现在kern/process/switch.S。
* tf：tf里保存了进程的中断帧。当进程从用户空间跳进内核空间的时候，进程的执行状态被保存在了中断帧中。系统调用可能会改变用户寄存器的值，我们可以通过调整中断帧来使得系统调用返回特定的值。

## 练习2 为新创建的内核线程分配资源

### 1.补充do_fork 函数

根据文档提示，do_fork函数的处理大致可以分为7步，下面我们来按步骤实现该函数：

```C
int
do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    int ret = -E_NO_FREE_PROC;
    struct proc_struct *proc;
    if (nr_process >= MAX_PROCESS) {
        goto fork_out;
    }
    ret = -E_NO_MEM;

    proc = alloc_proc();
    if(proc == NULL){
        goto fork_out;
    }
    proc->parent = current;

    if (setup_kstack(proc)) {
        goto bad_fork_cleanup_proc;
    }
    if(copy_mm(clone_flags, proc)){
        goto bad_fork_cleanup_kstack;
    }
    copy_thread(proc, stack, tf);
        bool intr_flag;
    local_intr_save(intr_flag);//屏蔽中断，intr_flag置为1
    {
        proc->pid = get_pid();//获取当前进程PID
        hash_proc(proc); //建立hash映射
        list_add(&proc_list, &(proc->list_link));//加入进程链表
        nr_process ++;//进程数加一
    }
    local_intr_restore(intr_flag);//恢复中断
    wakeup_proc(proc);
    ret = proc->pid;//返回当前进程的PID

fork_out:
    return ret;

bad_fork_cleanup_kstack:
    put_kstack(proc);
bad_fork_cleanup_proc:
    kfree(proc);
    goto fork_out;
}
```

1. 调用alloc_proc()函数申请内存块，如果失败，直接返回处理。
2. 调用setup_kstack()函数为进程分配一个内核栈。
3. 调用copy_mm()函数，复制父进程的内存信息到子进程。对于这个函数可以看到，
   进程proc复制还是共享当前进程current，是根据clone_flags来决定的，如果是clone_flags & CLONE_VM（为真），那么就可以拷贝。本实验中，仅仅是确定了一下当前进程的虚拟内存为空，并没有做其他事。
4. 调用copy_thread()函数复制父进程的中断帧和上下文信息。
5. 调用hash_proc()函数把新进程的PCB插入到哈希进程控制链表中，然后通过list_add函数把PCB插入到进程控制链表中，并把总进程数+1。在添加到进程链表的过程中，我们使用了local_intr_save()和local_intr_restore()函数来屏蔽与打开，保证添加进程操作不会被抢断。
6. 调用wakeup_proc()函数来把当前进程的state设置为PROC_RUNNABLE。
7. 返回新进程号。

### 2.请说明 ucore 是否做到给每个新 fork 的线程一个唯一的 id？请说明你的分析和理由。

我们可以查看实验中获取进程id的函数：get_pid(void)

```C
static int
get_pid(void) {
    static_assert(MAX_PID > MAX_PROCESS);
    struct proc_struct *proc;
    list_entry_t *list = &proc_list, *le;
    static int next_safe = MAX_PID, last_pid = MAX_PID;
    if (++ last_pid >= MAX_PID) {
        last_pid = 1;
        goto inside;
    }
    if (last_pid >= next_safe) {
    inside:
        next_safe = MAX_PID;
    repeat:
        le = list;
        while ((le = list_next(le)) != list) {
            proc = le2proc(le, list_link);
            if (proc->pid == last_pid) {
                if (++ last_pid >= next_safe) {
                    if (last_pid >= MAX_PID) {
                        last_pid = 1;
                    }
                    next_safe = MAX_PID;
                    goto repeat;
                }
            }
            else if (proc->pid > last_pid && next_safe > proc->pid) {
                next_safe = proc->pid;
            }
        }
    }
    return last_pid;
}
```

这段代码通过维护一个静态变量last_pid来实现为每个新fork的线程分配一个唯一的id。让我们逐步分析：

last_pid是一个静态变量，它会记录上一个分配的pid。
当get_pid函数被调用时，首先检查是否last_pid超过了最大的pid值（MAX_PID）。如果超过了，将last_pid重新设置为1，从头开始分配。
如果last_pid没有超过最大值，就进入内部的循环结构。在循环中，它遍历进程列表，检查是否有其他进程已经使用了当前的last_pid。如果发现有其他进程使用了相同的pid，就将last_pid递增，并继续检查。
如果没有找到其他进程使用当前的last_pid，则说明last_pid是唯一的，函数返回该值。
这样，通过这个机制，每次调用get_pid都会尽力确保分配一个未被使用的唯一pid给新fork的线程。

## 练习3 编写 proc_run 函数

### 1.完成proc_run()函数

```C
void
proc_run(struct proc_struct *proc) {
    if (proc != current) {
        bool flag;
        struct proc_struct *current_proc = current;
        struct proc_struct *next_proc = proc;
        local_intr_save(flag);
        {
            current = proc;
            lcr3(next_proc->cr3);
            switch_to(&(current_proc->context), &(next_proc->context));
        }
        local_intr_restore(flag);
    }
}
```

此函数基本思路是：

* 让 current指向 proc 内核线程initproc；
* 设置 CR3 寄存器的值为 proc 内核线程 initproc 的页目录表起始地址 next_proc->cr3，这实际上是完成进程间的页表切换；
* 由 switch_to函数完成具体的两个线程的执行现场切换，即切换各个寄存器。

值得注意的是，这里我们使用local_intr_save()和local_intr_restore()，作用分别是屏蔽中断和打开中断，以免进程切换时其他进程再进行调度，保护进程切换不会被中断。

### 2.本实验的执行过程中，创建且运行了几个内核线程？

在本实验中，创建且运行了2两个内核线程：

* idleproc：第一个内核进程，完成内核中各个子系统的初始化，之后立即调度，执行其他进程。
* initproc：用于完成实验的功能而调度的内核进程。

## 扩展练习 Challenge

### 1.说明语句local_intr_save(intr_flag);....local_intr_restore(intr_flag);是如何实现开关中断的？

在sync.h中定义了这两个函数。

```C
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

1. intr_save和local_intr_save：

* **intr_save：** 通过读取CSR控制和状态寄存器的sstatus中的值，并对比其是否被设置为了SIE=1，即中断使能位=1。如果中断使能位SIE是1，那么表示中断是被允许的；为0就是不允许的。因此如果中断本来是允许的，就会调用intr.h中的intr_disable禁用中断，否则直接返回，因此本身也不允许。
* **local_intr_save：** 宏定义为intr_save函数，用 do-while循环可以确保 x变量在__intr_save() 函数调用之后被正确赋值，无论中断是否被禁用。

2. intr_restore和local_intr_restore：

* **intr_restore：** 直接根据flag标志位是否为0，intr_enable()重新启用中断。
* **local_intr_restore：** 宏定义为intr_restore函数。
