idle进程创建子进程init.

init进程创建子进程user_main.

user_main函数中调用KERNEL_EXECVE函数.

KERNEL_EXECVE函数最终调用do_execve函数(中间过程为：KERNEL_EXECVE函数通过ebreak指令实现内核态系统调用，转到trap.c中异常处理结构体的BREAKPOINT的Case下--->调用syscall函数-->调用sys_exec函数-->调用do_execve函数)

do_execve函数实际在做的事情是在当前的进程下，停止原先正在运行的程序，开始执行一个新程序。PID不变，但是内存空间重新分配，执行的机器代码发生了改变。do_execve函数的内容如下：

```c
// do_execve - call exit_mmap(mm)&put_pgdir(mm) to reclaim memory space of current process
//           - call load_icode to setup new memory space accroding binary prog.
int
do_execve(const char *name, size_t len, unsigned char *binary, size_t size) {
    struct mm_struct *mm = current->mm;
    if (!user_mem_check(mm, (uintptr_t)name, len, 0)) {
        return -E_INVAL;
    }
    if (len > PROC_NAME_LEN) {
        len = PROC_NAME_LEN;
    }

    char local_name[PROC_NAME_LEN + 1];
    memset(local_name, 0, sizeof(local_name));
    memcpy(local_name, name, len);

    if (mm != NULL) {
        cputs("mm != NULL");
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    int ret;
    if ((ret = load_icode(binary, size)) != 0) {
        goto execve_exit;
    }
    set_proc_name(current, local_name);
    return 0;

execve_exit:
    do_exit(ret);
    panic("already exit: %e.\n", ret);
}
```

在这个函数中进程的替换是通过调用load_icode函数来实现的，替换的内容包括内存空间中的数据、设置中断帧和栈，这个函数的完整代码如下：

```c
/* load_icode - load the content of binary program(ELF format) as the new content of current process
 * @binary:  the memory addr of the content of binary program
 * @size:  the size of the content of binary program
 */
static int
load_icode(unsigned char *binary, size_t size) {
    if (current->mm != NULL) {
        panic("load_icode: current->mm must be empty.\n");
    }

    int ret = -E_NO_MEM;
    struct mm_struct *mm;
    //(1) create a new mm for current process
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    //(2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    //(3) copy TEXT/DATA section, build BSS parts in binary to memory space of process
    struct Page *page;
    //(3.1) get the file header of the bianry program (ELF format)
    struct elfhdr *elf = (struct elfhdr *)binary;
    //(3.2) get the entry of the program section headers of the bianry program (ELF format)
    struct proghdr *ph = (struct proghdr *)(binary + elf->e_phoff);
    //(3.3) This program is valid?
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }

    uint32_t vm_flags, perm;
    struct proghdr *ph_end = ph + elf->e_phnum;
    for (; ph < ph_end; ph ++) {
    //(3.4) find every program section headers
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            // continue ;
        }
    //(3.5) call mm_map fun to setup the new vma ( ph->p_va, ph->p_memsz)
        vm_flags = 0, perm = PTE_U | PTE_V;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
        // modify the perm bits here for RISC-V
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap;
        }
        unsigned char *from = binary + ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

     //(3.6) alloc memory, and  copy the contents of every program section (from, from+end) to process's memory (la, la+end)
        end = ph->p_va + ph->p_filesz;
     //(3.6.1) copy TEXT/DATA section of bianry program
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memcpy(page2kva(page) + off, from, size);
            start += size, from += size;
        }

      //(3.6.2) build BSS section of binary program
        end = ph->p_va + ph->p_memsz;
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }
    //(4) build user stack memory
    vm_flags = VM_READ | VM_WRITE | VM_STACK;
    if ((ret = mm_map(mm, USTACKTOP - USTACKSIZE, USTACKSIZE, vm_flags, NULL)) != 0) {
        goto bad_cleanup_mmap;
    }
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-2*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-3*PGSIZE , PTE_USER) != NULL);
    assert(pgdir_alloc_page(mm->pgdir, USTACKTOP-4*PGSIZE , PTE_USER) != NULL);
    
    //(5) set current process's mm, sr3, and set CR3 reg = physical addr of Page Directory
    mm_count_inc(mm);
    current->mm = mm;
    current->cr3 = PADDR(mm->pgdir);
    lcr3(PADDR(mm->pgdir));

    //(6) setup trapframe for user environment
    struct trapframe *tf = current->tf;
    // Keep sstatus
    uintptr_t sstatus = tf->status;
    memset(tf, 0, sizeof(struct trapframe));
    /* LAB5:EXERCISE1 YOUR CODE
     * should set tf->gpr.sp, tf->epc, tf->status
     * NOTICE: If we set trapframe correctly, then the user level process can return to USER MODE from kernel. So
     *          tf->gpr.sp should be user stack top (the value of sp)
     *          tf->epc should be entry point of user program (the value of sepc)
     *          tf->status should be appropriate for user program (the value of sstatus)
     *          hint: check meaning of SPP, SPIE in SSTATUS, use them by SSTATUS_SPP, SSTATUS_SPIE(defined in risv.h)
     */
    tf->gpr.sp = USTACKTOP;
    tf->epc = elf->e_entry;

    ///SSTATUS_SPP要设置为0，以保证在sret后进入用户模式;SSTATUS_SPIE要设置为1，以保证在sret后使能中断
    tf->status = (sstatus | SSTATUS_SPIE) & ~SSTATUS_SPP;
    

    ret = 0;
out:
    return ret;
bad_cleanup_mmap:
    exit_mmap(mm);
bad_elf_cleanup_pgdir:
    put_pgdir(mm);
bad_pgdir_cleanup_mm:
    mm_destroy(mm);
bad_mm:
    goto out;
}

```

所做的操作分为如下几步：

​		Step1:创建新的mm结构体来管理后续会分配的内存空间.

​		Step2:申请一个Page的内存空间，用于存放512个页表项.(也就是为mm创建了一个PDT，PageDirectoryTable)

​		Step3:将binary指向的内存区域中的数据（也就是要执行的文件）拷贝到当前进程的内存空间中.

​		Step4:设置用户栈内存空间

​		Step5:重新设置cr3寄存器(定位页表)的值，因为在替换完进程后，要使用新的页表.还有重新设置当前进程的mm结构体的值，都要替换成新的.

​		Step6:设置新进程的中断帧，特别需要注意的是栈指针sp和程序计数器epc寄存器的值，根据中断帧中这两个值的设计，当这个进程退出异常处理时就会转到epc寄存器中指定的位置继续运行，而epc寄存器中的值在这里被我们设置为了这个新加载的文件的entrypoint入口点，因此会执行这个新的文件内容.另外，由于栈从高地址向低地址增长，因此栈指针sp被设置为栈顶.

------

在这里要执行的新的程序就是第一个用户程序,即exit.c的main函数.注意！到这里就正式进入了用户态.

```c
#Program:exit.c

#include <stdio.h>
#include <ulib.h>

int magic = -0x10384;

int
main(void) {
    int pid, code;
    cprintf("I am the parent. Forking the child...\n");
    if ((pid = fork()) == 0) {
        cprintf("I am the child.\n");
        yield();
        yield();
        yield();
        yield();
        yield();
        yield();
        yield();
        exit(magic);
    }
    else {
        cprintf("I am parent, fork a child pid %d\n",pid);
    }
    assert(pid > 0);
    cprintf("I am the parent, waiting now..\n");

    assert(waitpid(pid, &code) == 0 && code == magic);
    assert(waitpid(pid, &code) != 0 && wait() != 0);
    cprintf("waitpid %d ok.\n", pid);

    cprintf("exit pass.\n");
    return 0;
}


```

在exit.c中会先执行fork函数创建子进程，这个子进程也属于用户进程.调用fork函数后真正创建子进程的函数调用过程为：***fork-->sys_fork-->syscall(SYS_fork)***-->通过ecall(SYS_fork)中断进入异常处理-->转到trap.c异常处理结构体的USER_ECALL的Case下-->执行syscall()-->调用sys_fork()-->最终调用do_fork().

*注意!这里会有权限的转换，当通过ecall指令进入异常处理中后就由用户态转为了内核态*(为了便于区分，用户态的函数均被设置为斜体且加粗).

do_fork函数会fork一份子进程，并返回子进程的pid，返回值的传递过程与上述的函数调用过程恰好是相反的：do_fork函数return-->sys_fork函数return-->***syscall函数得到返回值(中断帧的a0寄存器保存返回值)-->syscall函数得到返回值(通过a0寄存器)-->sys_fork函数得到返回值-->fork函数得到返回值.***

------

fork函数调用完成后，exit的main函数会继续向下执行，注意到下面紧接着的代码为waitpid,这个用户态的函数最终通过调用do_wait函数来实现，权限转换和函数调用过程与fork函数是类似的，不再赘述.既然最终会调用do_wait函数，我们就来分析这个函数：

```c
// do_wait - wait one OR any children with PROC_ZOMBIE state, and free memory space of kernel stack
//         - proc struct of this child.
// NOTE: only after do_wait function, all resources of the child proces are free.
int
do_wait(int pid, int *code_store) {
    struct mm_struct *mm = current->mm;
    if (code_store != NULL) {
        if (!user_mem_check(mm, (uintptr_t)code_store, sizeof(int), 1)) {
            return -E_INVAL;
        }
    }

    struct proc_struct *proc;
    bool intr_flag, haskid;
repeat:
    haskid = 0;
    if (pid != 0) {
        proc = find_proc(pid);
        if (proc != NULL && proc->parent == current) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    else {
        proc = current->cptr;
        for (; proc != NULL; proc = proc->optr) {
            haskid = 1;
            if (proc->state == PROC_ZOMBIE) {
                goto found;
            }
        }
    }
    if (haskid) {
        current->state = PROC_SLEEPING;
        current->wait_state = WT_CHILD;
        schedule();
        if (current->flags & PF_EXITING) {
            do_exit(-E_KILLED);
        }
        goto repeat;
    }
    return -E_BAD_PROC;

found:
    if (proc == idleproc || proc == initproc) {
        panic("wait idleproc or initproc.\n");
    }
    if (code_store != NULL) {
        *code_store = proc->exit_code;
    }
    local_intr_save(intr_flag);
    {
        unhash_proc(proc);
        remove_links(proc);
    }
    local_intr_restore(intr_flag);
    put_kstack(proc);
    kfree(proc);
    return 0;
}
```

这个函数在尝试找出当前进程的处于ZOMBIE状态的子进程并清理它们.但事实上这样的子进程不一定存在，但是没有关系，如果这种子进程不存在，那么这个函数就尝试将CPU控制权转移给当前进程的子进程，这是通过将当前进程设置为Sleeping且等待CHILD状态并调用schedule函数实现的.

紧接着上面的分析，我们刚才提到do_wait函数是在fork函数被调用后调用的，那么在这种情况下调用do_wait函数再在do_wait函数中进一步调用schedule函数，很自然的控制权就被移交给了刚刚fork出来的子进程.

那么这个子进程会从哪个位置开始执行呢？schedule函数通过调用proc_run函数来运行新的进程，而在proc_run函数中会调用switch_to函数来进行上下文的切换,那么在上下文切换后，ra寄存器的值就变为了新进程中这个寄存器被设置的值，这个寄存器值是在do_fork创建新进程时调用copy_thread函数来实现的.

```c
// copy_thread - setup the trapframe on the  process's kernel stack top and
//             - setup the kernel entry point and stack of process
static void
copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf) {
    proc->tf = (struct trapframe *)(proc->kstack + KSTACKSIZE) - 1;
    *(proc->tf) = *tf;

    // Set a0 to 0 so a child process knows it's just forked
    proc->tf->gpr.a0 = 0;
    proc->tf->gpr.sp = (esp == 0) ? (uintptr_t)proc->tf : esp;
    //如果 esp（用户栈指针）为 0，表示创建的是内核线程，栈指针设置为 Trap Frame 的地址；
    //否则，使用提供的 esp 作为栈指针。

    proc->context.ra = (uintptr_t)forkret;
    proc->context.sp = (uintptr_t)(proc->tf);
}
```

可以看到，ra寄存器的值被设置为forkret函数的地址，那么当switch_to函数执行完毕后再执行ret指令时，就会跳转到forkret函数执行，我们再来看这个forkret函数的作用：

```c
static void
forkret(void) {
    forkrets(current->tf);
}
```

```bash
.globl __trapret
__trapret:
    RESTORE_ALL
    # return from supervisor call
    sret
 
    .globl forkrets
forkrets:
    # set stack to this new process's trapframe
    move sp, a0
    j __trapret
```

forkret函数将当前的中断帧作为参数，然后跳转到forkrets处，而在forkrets处，首先将a0(也就是参数)的值赋值给sp栈指针，接着跳到trapret处执行，在trapret处会执行RESTORE_ALL执行，这个指令将会从栈中恢复所有寄存器的值，再结合当前栈指针的位置，也就是会根据当前的中断帧中的内容来恢复寄存器的值.同样在copy_thread函数中，我们看到当前进程的中断帧被设置为copy_thread函数调用时参数中中断帧tf的内容，再往回追溯也就是do_fork函数调用时中断帧参数tf的值，再往回追溯其实就是在调用do_fork函数时当前进程的中断帧.因此因此，最终这个fork出来的子进程拿到CPU控制权后就会紧接着当前进程的因调用fork函数而产生中断的那个位置继续运行.

------

另外这里要指明发生中断时的处理过程.当发生中断时，众所周知的是要进入trap.c中根据异常原因进行异常处理，但在此之前理论上还应该有保存中断帧的操作，这是在下面的代码中实现的.

```c
/* idt_init - initialize IDT to each of the entry points in kern/trap/vectors.S */
void
idt_init(void) {
    extern void __alltraps(void);
    /* Set sscratch register to 0, indicating to exception vector that we are
     * presently executing in the kernel */
    write_csr(sscratch, 0);
    /* Set the exception vector address */
    write_csr(stvec, &__alltraps);
    /* Allow kernel to access user memory */
    set_csr(sstatus, SSTATUS_SUM);
}
```

首先我们注意到在中断初始化函数中声明了一个外部变量alltraps，并在之后通过write_csr函数将这个外部变量alltraps的地址保存在stvec寄存器中，这意味着当任何中断/异常发生时，处理器就会转到alltraps函数执行.

```bash
.globl __alltraps
__alltraps:
    SAVE_ALL

    move  a0, sp
    jal trap
    # sp should be the same as before "jal trap"
```

```c

/* *
 * trap - handles or dispatches an exception/interrupt. if and when trap() returns,
 * the code in kern/trap/trapentry.S restores the old CPU state saved in the
 * trapframe and then uses the iret instruction to return from the exception.
 * */
void
trap(struct trapframe *tf) {
    // dispatch based on what type of trap occurred
//    cputs("some trap");
    if (current == NULL) {
        trap_dispatch(tf);
    } else {
        struct trapframe *otf = current->tf;
        current->tf = tf;

        bool in_kernel = trap_in_kernel(tf);

        trap_dispatch(tf);

        current->tf = otf;
        if (!in_kernel) {
            if (current->flags & PF_EXITING) {
                do_exit(-E_KILLED);
            }
            if (current->need_resched) {
                schedule();
            }
        }
    }
}
```

紧接着我们看到，在alltraps处所做的操作就是SAVE_ALL，保存所有的寄存器，然后将sp栈指针的值赋值给a0然后跳转到trap处.再结合trap函数的实现，a0在这里作为参数也就是刚才所保存的中断帧.

------

最后，当这个子进程执行结束后CPU控制权又将如何转移？

在exit.c的main函数中我们注意到，当子进程结束后，会调用exit()函数，这个函数最终通过系统调用do_exit函数来退出。这个系统调用函数如下所示：

```c
// do_exit - called by sys_exit
//   1. call exit_mmap & put_pgdir & mm_destroy to free the almost all memory space of process
//   2. set process' state as PROC_ZOMBIE, then call wakeup_proc(parent) to ask parent reclaim itself.
//   3. call scheduler to switch to other process
int
do_exit(int error_code) {
    if (current == idleproc) {
        panic("idleproc exit.\n");
    }
    if (current == initproc) {
        panic("initproc exit.\n");
    }
    struct mm_struct *mm = current->mm;
    if (mm != NULL) {
        lcr3(boot_cr3);
        if (mm_count_dec(mm) == 0) {
            exit_mmap(mm);
            put_pgdir(mm);
            mm_destroy(mm);
        }
        current->mm = NULL;
    }
    current->state = PROC_ZOMBIE;
    current->exit_code = error_code;
    bool intr_flag;
    struct proc_struct *proc;
    local_intr_save(intr_flag);
    {
        proc = current->parent;
        if (proc->wait_state == WT_CHILD) {
            wakeup_proc(proc);
        }
        while (current->cptr != NULL) {
            proc = current->cptr;
            current->cptr = proc->optr;
    
            proc->yptr = NULL;
            if ((proc->optr = initproc->cptr) != NULL) {
                initproc->cptr->yptr = proc;
            }
            proc->parent = initproc;
            initproc->cptr = proc;
            if (proc->state == PROC_ZOMBIE) {
                if (initproc->wait_state == WT_CHILD) {
                    wakeup_proc(initproc);
                }
            }
        }
    }
    local_intr_restore(intr_flag);
    schedule();
    panic("do_exit will not return!! %d.\n", current->pid);
}
```

do_exit函数所做的工作就是清理当前进程的资源，然后唤醒(wakeup_proc函数，但注意这个函数只是将这个进程的状态转为了RUNNABLE，但这时并未获取CPU控制权)它的父进程完成剩余的回收工作.做完这些后会调用schedule函数，再次选择新的进程执行.很显然由于这时刚刚父进程被唤醒了，那么控制权自然就被移交到了父进程的手里，然后父进程的也执行完毕，再重复上述过程，总体的流程就是：

fork()出的子进程退出，唤醒它的父进程，也就是user_main-->user_main进程退出，唤醒它的父进程，也就是init_proc-->init_proc退出，但注意这时在执行do_exit函数时就不会像之前那些进程一样，因为在do_exit函数中，针对idle和init这两个进程有特殊的处理:一个panic.

