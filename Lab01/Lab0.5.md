![image-20240926211623878](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926211623878.png)

使用 QEMU 模拟器和 GDB 调试器来模拟计算机硬件加电并进行调试

## 1.启动 QEMU 并等待 GDB 连接
cd到`/home/xu/code/UcoreLab/UcoreLab`，使用 Makefile 中的 `make` 命令编译操作系统内核
```
make debug
```
查看 Makefile 文件，从 168 行开始
```
debug: $(UCOREIMG) $(SWAPIMG) $(SFSIMG)
        $(V)$(QEMU) \
                -machine virt \
                -nographic \
                -bios default \
                -device loader,file=$(UCOREIMG),addr=0x80200000\
                -s -S
```
（1）依赖关系处理：
- **`$(UCOREIMG)`**: *编译并链接所有源文件（`.c` 和 `.S`），生成内核映像文件* `ucore.img`。
- **`$(SWAPIMG)`**: 交换空间的映像文件，用于存储暂时不在内存中的数据，帮助管理内存。
- **`$(SFSIMG)`**: 文件系统映像，包含操作系统的文件系统结构和文件，供操作系统访问。

（2）加载内核到 QEMU：
1. QEMU 初始化：
    `$(QEMU)`：表示启动的 QEMU 模拟器。这行代码会调用 QEMU 初始化硬件环境。
    
    `-machine virt`: 指定模拟的机器类型为虚拟机（virt）。 
    `-nographic`: 以无图形界面模式运行。
    
2. 加载固件：
    `-bios default`：QEMU 会加载固件（默认 OpenSBI）到指定的内存地址 `0x80000000`。
3. 执行复位代码：
    QEMU 启动后会自动将 PC 设置为复位地址 `0x1000`，开始执行复位代码。
4. 跳转到固件：
    复位代码执行完成后，将 PC 设置为固件加载地址 `0x80000000`。
5. 执行固件代码：
    OpenSBI 进一步初始化硬件，包括设置中断、内存、设备等。
6. 加载操作系统内核：
    `-device loader,file=$(UCOREIMG),addr=0x80200000`：QEMU 加载操作系统内核`$(UCOREIMG)`到指定的内存地址`0x80200000`。
7. 跳转到操作系统内核：
    固件将 PC 设置为 `0x80200000`，跳转到操作系统内核的入口点，开始执行内核代码。
8. 操作系统启动：
    操作系统内核开始执行，进行进一步的系统初始化，最终启动用户空间程序。
9. 使用远程调试：
    `-s -S`：这两个参数让 QEMU 进入等待 GDB 连接的状态，等待调试命令后再开始执行。

>QEMU 模拟器模拟整个计算机系统，包括一块riscv64的CPU、内存、存储设备和借助电脑的键盘和显示屏来模拟命令行的输入和输出
## 2.启动 GDB 并连接到 QEMU
安装共享库
```
sudo apt-get install libncurses5
sudo apt-get install libpython2.7
```
打开一个新的终端窗口，启动 GDB 调试
```
make gdb
```

查看 Makefile 文件，从 176 行开始
```
gdb:
    riscv64-unknown-elf-gdb \
    -ex 'file bin/kernel' \
    -ex 'set arch riscv:rv64' \
    -ex 'target remote localhost:1234'
```
1. 启动 GDB：
    `make gdb` 会调用 `riscv64-unknown-elf-gdb` 命令来启动 RISC-V 架构的 GDB 调试器。
2. 加载内核文件：
    `-ex 'file bin/kernel'` ：加载之前编译生成的内核二进制文件 `bin/kernel` 。
3. 设置架构：
    `-ex 'set arch riscv:rv64'` ：设置 GDB 的目标架构为 RISC-V 64 位。
4. 连接到远程目标：
     `-ex 'target remote localhost:1234'` ：连接到 QEMU 监听的端口（默认是本地主机的 1234 端口）进行远程调试。
## 3.调试内核
一旦 GDB 成功连接到 QEMU，可以开始调试内核。

显示即将执行的10条汇编指令，PC在 `0x1000` 处，即复位地址
```
x/10i $pc
```
![image-20240926210725533](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926210725533.png)

`auipc`  "Add Upper Immediate to PC"，即 "将程序计数器（PC）的高位加上立即数",将PC的高20位与一个20位的立即数相加，然后将结果存储在目标寄存器中。

`csrr` 是 RISC-V 指令集中用于读取控制和状态寄存器（CSR）的指令。指令 `csrr a0, mhartid` 的含义是将 `mhartid` 寄存器的值读取到寄存器 `a0` 中。

`mhartid`（Machine Hart ID Register）是 RISC-V 架构中的一个 CSR，用于存储当前硬件线程（hart）的唯一标识符。在多核处理器中，每个核心可能有一个或多个硬件线程，每个硬件线程都有一个唯一的 `mhartid`。这个寄存器通常用于在多核系统中识别不同的硬件线程

`unimp` 当作0x00.

CPU上电->bootloader被加载到0x80000000;内核镜像被加载到0x80200000;PC被赋予复位地址；复位地址代码做什么？：将计算机系统各组件(处理器、内存等)置于初始状态。跳转到0x80000000（bootloader的地址）

![image-20240926215538484](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926215538484.png)

`auipc`  "Add Upper Immediate to PC"，即 "将程序计数器（PC）的高位加上立即数",将PC的高20位与一个20位的立即数相加，然后将结果存储在目标寄存器中。即sp=$PC(0x80200000)+0x3000=0x80203000

验证：执行后sp的值为：

![image-20240926215857655](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926215857655.png)

![image-20240926220223417](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926220223417.png)

这两条指令，作用分别为将label：bootstacktop的值加载到sp(分配内核栈)；tail相当于跳转到kern_init(真正的入口点)，不过做了一些尾调用优化.

真正的入口点这里做的事情是：(不过这里的头文件以及头文件内部的函数都需要自己手动实现(因为没有操作系统的支持所以不能调用标准库))

![image-20240926220848868](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926220848868.png)

如何手动实现呢？前提条件是OpenSBI提供了一些接口可以供我们在编写内核时使用，因此思路就是利用这些接口实现输出，然后将该功能一层层抽象、封装。

在这里使用ecall(environment call)指令来调用OpenSBI，最底层的实现是这样的：

![image-20240926222649326](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926222649326.png)

sbi_console_putchar()-->(封装)cons_putc-->(封装)cputch()-->(封装)cputs

cprintf()调用vcprintf()；vcprintf()调用vprintfmt();vprintfmt()调用putch()

总之就是各层封装封装封装。。。

## 4.完成调试
完成调试后，你可以使用以下命令退出 GDB：
```text
(gdb) quit
```

### ![image-20240926210806192](C:\Users\20605\AppData\Roaming\Typora\typora-user-images\image-20240926210806192.png)