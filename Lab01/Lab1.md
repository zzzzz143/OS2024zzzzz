![image-20240927132555312](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927132555312.png)

##### RISCV权限模式

###### 	M-Mode:

在 M 模式下运行的 hart (hardware thread,硬件线程)对内存,I/O 和一些对于启动和配置系统来说必要的底层功能有着完全的使用权。

默认情况下,发生所有异常(不论在什么权限模式下)的时候,控制权都会被移交到 M 模式的异常处理程序

##### 	S-Mode(监管者模式):

S-mode是支持现代类 Unix 操作系统的权限模式



#### **特殊寄存器** CSR(**控制状态寄存器** **Control and Status Registers)**

​	**sstatus寄存器(Supervisor Status Register):**

​		包含二进制位SIE(supervisor interrupt enable),数值为0的时候，如果当程序在S态运行，将禁用全部中断

​		包含另一个二进制位UIE(user interrupt enable),在置零的时候禁止用户态程序产生中断

​	**stvec寄存器(Supervisor Trap Vector Base Address Register)：中断向量表基址寄存器**：

​		其最低位的两个二进制位被用来编码一个“模式”，如果是“00”就说明其余62(SXLEN((SXLEN是`stval`寄存器的位数))-2)个二进制位存储的是唯一的中断处理程序的地址；如果是“01”说明其余62个二进制位存储的是中断向量表基址，通过不同的异常原因来索引中断向量表.         62个二进制位表示地址？不行!需要在较高的62位后补两个0.

​	**sepc寄存器(supervisor exception program counter):**

​		记录触发中断的那条指令的地址

​	**scause寄存器：**

​		记录中断发生的原因，还会记录该中断是不是一个外部中断

​	**stval寄存器:**

​		记录一些中断处理所需要的辅助信息，比如指令获取(instruction fetch)、访存、缺页异常，它会把发生问题的目标地址或者出错的指令记录下来，这样我们在中断处理程序中就知道处理目标了



#### 特权指令：

​	ecall:在 S 态执行这条指令时，会触发一个 ecall-from-s-mode-exception，从而进入 M 模式中的中断处理流程（如设置定时器等）;在 U 态执行这条指令时，会触发一个 ecall-from-u-mode-exception，从而进入 S 模式中的中断处理流程（常用来进行系统调用）

​	sret:用于 S 态中断返回到 U 态，实际作用为pc←sepc

​	ebreak:触发一个断点中断从而进入中断处理流程

​	mret:用于 M 态中断返回到 S 态或 U 态，实际作用为pc←mepc



#### 中断分类

**异常(Exception)**，指在执行一条指令的过程中发生了错误，此时我们通过中断来处理错误。最常见的异常包括：访问无效内存地址、执行非法指令(除零)、发生缺页等。他们有的可以恢复(如缺页)，有的不可恢复(如除零)，只能终止程序执行。

**陷入(Trap)**，指我们主动通过一条指令停下来，并跳转到处理函数。常见的形式有通过ecall进行系统调用(syscall)，或通过ebreak进入断点(breakpoint)。

**外部中断(Interrupt)**，简称中断，指的是 CPU 的执行过程被外设发来的信号打断，此时我们必须先停下来对该外设进行处理。典型的有定时器倒计时结束、串口收到数据等。

中断过程：保存上下文-->转到中断处理函数-->中断函数处理完毕-->恢复上下文-->返回原程序

```bash
qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
#$(V)$(QEMU) -kernel $(UCOREIMG) -nographic
$(V)$(QEMU) \
	-machine virt \
	-nographic \
	-bios default \
	-device loader,file=$(UCOREIMG),addr=0x80200000
```

保存上下文需要保存什么？通用寄存器的值、某些CSR( **控制状态寄存器** **Control and Status Registers**)寄存器的值(RISCV不能直接从CSR写到内存, 需要csrr把CSR读取到通用寄存器，再从通用寄存器STORE到内存).

为了方便保存数据定义了如下结构体：

![image-20240927153712254](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927153712254.png)

那么接下来，中断处理函数如何做？(这次实验要实现的时钟中断：每隔若干个时钟周期执行一次的程序).(根据上一次实验的经验应该也是调用底层硬件接口，然后逐层封装)

​		O不对，首先你应该初始化你要用到的中断、你要用到的时钟等等等......

```c++
// kern/init/init.c
#include <trap.h>
int kern_init(void) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);

    cons_init();  // init the console

    const char *message = "(THU.CST) os is loading ...\n";
    cprintf("%s\n\n", message);

    print_kerninfo();

    // grade_backtrace();

    //trap.h的函数，初始化中断
    idt_init();  // init interrupt descriptor table

    //clock.h的函数，初始化时钟中断
    clock_init();  
    //intr.h的函数，使能中断
    intr_enable();  
    while (1)
        ;
}
```

**Kern_init函数又是如何被调用的？**其实仔细看这一函数就是在Lab0.5部分的代码的基础上，在后面增加了初始化等等部分的函数

嗯....那么初始化又是在做什么事情？？？？？？

**中断初始化：**设置了sscratch；设置stvec寄存器(中断向量表基址寄存器**：)的值为__alltraps标签(中断入口点)的地址

![image-20240927150046386](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927150046386.png)

**时钟初始化：**

![image-20240927150149154](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927150149154.png)	

时钟初始化在设置第一次时钟中断

首先我们从最底层的硬件开始，RISCV对时钟中断的硬件支持包括：

```
OpenSBI提供的sbi_set_timer()接口，可以传入一个时刻，让它在那个时刻触发一次时钟中断
```

```
rdtime伪指令，读取一个叫做time的CSR的数值，表示CPU启动之后经过的真实时间。在不同硬件平台，时钟频率可能不同。在QEMU上，这个时钟的频率是10MHz, 每过1s, rdtime返回的结果增大10000000
```

OK有了上面两个硬件支持，那么实现方式就很清楚了：使用sbi_set_timer()函数来设置中断时刻。但是又有一个问题是一次只能设置一个中断，这又如何解决呢？思路就是在处理中的时候再次设置下一个时钟中断时刻(反正代码是你自己来实现的).

看一下如何实现的：

![image-20240927143151142](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927143151142.png)

get_cycles()又是如何实现的？

![image-20240927143211563](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927143211563.png)

get_cycles()用于获取CPU周期计数，其中用到了rdtime获取CPU周期数。

再OK一次，那么当设置的中断时刻来临的时候又会发生什么样的函数调用呢？

##### 设置第一次时钟中断(在时钟初始化时完成的)--->

![image-20240927140554485](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927140554485.png)

​	sbi_call函数长啥样？

##### 中断时刻来临--->触发中断--->跳转到__alltraps标记处(这是啥呢？这是中断入口点呐！)--->(还记得中断过程吗？往上看)下一次跳转就跳转到了中断处理函数trap()--->

trap()函数长这样：

![image-20240927141212281](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927141212281.png)

trap()函数会调用trap_dispatch()函数，trap_dispatch()函数把异常处理的工作分发给了interrupt_handler()，exception_handler()，这俩函数又长啥样？

```c++
void interrupt_handler(struct trapframe *tf) {
    intptr_t cause = (tf->cause << 1) >> 1; //抹掉scause最高位代表“这是中断不是异常”的1
    switch (cause) {
        case IRQ_U_SOFT:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_SOFT:
            cprintf("Supervisor software interrupt\n");
            break;
        case IRQ_H_SOFT:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_SOFT:
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_TIMER:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_TIMER:
            //时钟中断
            /* LAB1 EXERCISE2   YOUR CODE :  */
            /*(1)设置下次时钟中断
             *(2)计数器（ticks）加一
             *(3)当计数器加到100的时候，我们会输出一个`100ticks`表示我们触发了100次时钟中断，同时打印次数（num）加一
            * (4)判断打印次数，当打印次数为10时，调用<sbi.h>中的关机函数关机
            */
            break;
        case IRQ_H_TIMER:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_TIMER:
            cprintf("Machine software interrupt\n");
            break;
        case IRQ_U_EXT:
            cprintf("User software interrupt\n");
            break;
        case IRQ_S_EXT:
            cprintf("Supervisor external interrupt\n");
            break;
        case IRQ_H_EXT:
            cprintf("Hypervisor software interrupt\n");
            break;
        case IRQ_M_EXT:
            cprintf("Machine software interrupt\n");
            break;
        default:
            print_trapframe(tf);
            break;
    }
}

void exception_handler(struct trapframe *tf) {
    switch (tf->cause) {
        case CAUSE_MISALIGNED_FETCH:
            break;
        case CAUSE_FAULT_FETCH:
            break;
        case CAUSE_ILLEGAL_INSTRUCTION:
            //非法指令异常处理
            /* LAB1 CHALLENGE3   YOUR CODE :  */
            /*(1)输出指令异常类型（ Illegal instruction）
             *(2)输出异常指令地址
             *(3)更新 tf->epc寄存器
             */
            break;
        case CAUSE_BREAKPOINT:
            //非法指令异常处理
            /* LAB1 CHALLLENGE3   YOUR CODE :  */
            /*(1)输出指令异常类型（ breakpoint）
             *(2)输出异常指令地址
             *(3)更新 tf->epc寄存器
             */
            break;
        case CAUSE_MISALIGNED_LOAD:
            break;
        case CAUSE_FAULT_LOAD:
            break;
        case CAUSE_MISALIGNED_STORE:
            break;
        case CAUSE_FAULT_STORE:
            break;
        case CAUSE_USER_ECALL:
            break;
        case CAUSE_SUPERVISOR_ECALL:
            break;
        case CAUSE_HYPERVISOR_ECALL:
            break;
        case CAUSE_MACHINE_ECALL:
            break;
        default:
            print_trapframe(tf);
            break;
    }
}
```

这里只是简单地根据`scause`的数值更仔细地分了下类，做了一些输出就直接返回了，这只是一种实现形式，当然你也可以改写别的代码让它干别的事。

![image-20240927141544158](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927141544158.png)

OIKnow,这一部分要实现的时钟中断异常处理没有实现，这就是这次实验的任务。

发现这个trap.c里还提供了这些函数可以使用(还怪好的):

![image-20240927141834108](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927141834108.png)

OK那么就完善代码

![image-20240927143630089](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927143630089.png)

##### 如果你在中断处理函数中没有shut_down关机的话，那接下来：函数interrupt_handler()执行完了---->trap_dispatch()执行完了---->trap()执行完了--->返回到trspentry.S恢复上下文，中断处理结束--->恢复原程序的执行路径。 [trapentry.S](..\riscv64-ucore-labcodes\riscv64-ucore-labcodes\lab1\kern\trap\trapentry.S) 

OK最后运行试一下：make qemu

可以看到打印了10行`100 ticks`，然后自动关机

![image-20240927151731590](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240927151731590.png)

```tex
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
100 ticks
```

```bash

qemu: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
#	$(V)$(QEMU) -kernel $(UCOREIMG) -nographic
	$(V)$(QEMU) \
		-machine virt \
		-nographic \
		-bios default \
		-device loader,file=$(UCOREIMG),addr=0x80200000
```

