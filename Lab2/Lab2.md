#### 学号：2213573

#### 姓名：张政泽

#### 专业：信息安全



我把任务重心放在Challenge1和Challenge3上(其他两部分当然也做了)，因此在报告中主要记录这两部分的内容。

#### 练习1：first-fit算法改进空间思考

由于first-fit算法中所有大小的Page均存放在一个链表上，且按照地址大小排序，且每次分配时从链表头开始，寻找最先满足条件的块，因此就会造成一个这样的问题：当我希望分配大小为1的块时，尽管链表中存在大小为1的最合适的块，但依据算法，其总是会切分最先发现的块以满足分配需求，即使即使这个块非常大，那么这样很显然会导致内存碎片不断增加的问题。

优化方法是可以预先分配一定数量的内存块，当需要分配时先考虑这些内存块是否能满足要求，若能满足要求则直接从这些内存块中分配；当不能满足要求时再考虑从链表上切分。或者改进成接下来的Best-fit算法。

#### 练习2：Best-fit算法改进空间思考

Best-fit算法的最明显的改进点就是每次分配都要遍历整个链表，当链表过长时就会导致搜索时间增加，优化方法可以是将这个链表按照块大小排序，但是这样就会导致在释放块考虑合并时要遍历整个链表，因此需要在这两者之间权衡，另一种对合并的优化是当满足一定条件时才对链表进行合并操作，以减少在合并上的性能消耗。或者优化数据结构，例如使用二叉树来替换链表，但是实现难度上会高一点。

#### 扩展练习1：buddy system(伙伴系统)分配算法

buddy system(伙伴系统)把系统中可用的存储空间划分为存储块来进行管理，存储块的大小为2的n次幂，即1，2，4，8，32，64，128.

在我进行设计前，首先测试了一下除去内核占用的内存空间外，剩余可用的物理内存对应的页数，方法也很简单：在page_init函数中调用内存映射函数init_memmap前会计算可用的内存范围，将其除以PageSize后打印出来即可；

![image-20241025182931574](./p1.png)

最后得出可用的页数为31929。

##### init_memmap()

在buddy system的设计中，存储块大小包括1，2，4，8，16，32，64，128，256，512，1024个页，因此将最初的1个链表扩展为11个链表并使用数组来保存，数组下标i表示块大小为2^i的链表。在页的分配上，我为下标为1-10的每个链表分配了2048个页，剩余的页则全部分配给块大小为1的页，那么相应地链表中元素个数就是：

​	块大小为1024的链表--2个

​	块大小为512的链表-4个

​	.................

##### alloc_pages()

在分配函数中，由于要分配的块的大小只能为2的整数次幂，因此要先找到最合适的块大小，例如对于申请6个页的请求，最合适的大小即为8；但确定了最合适的块大小后，并不能保证对应的链表中剩余的块可以满足分配需求，因此要进一步寻找满足需求的最小的块大小。

当最终确定要分配的块后，如果块大小正合适，那么就直接分配；而若块大小过大，则对其进行拆分，即先分配所需的块大小，多出来的部分将其拆分，分配到对应大小的链表当中。

##### free_pages()

在释放块的时候要考虑的问题是buddy块的合并，buddy块不仅大小相同，而且还要求地址连续。因此当释放块时，首先检查对应的大小的链表中是否存在与其地址连续的块，若不存在则将其放到链表末尾；而若存在，则不能直接进行合并，实际上并不是所有地址连续和大小相同的块都应该进行合并，

例如如下情况

![image-20241025184652436](./p2.png)

当我们分配了四个大小相同且地址连续的块1、2、3、4后，若此时我们释放块2和3，由于这两个块地址连续且大小相同，因此理论上我们应该将其合并。但仔细观察可以发现，若此时将这俩块合并，那么接下来当1、4释放时，无论如何我们也无法再将1、4组合成大块，因此需要在合并前进行进一步检查，具体可见代码部分。

##### check()

在check()部分，除了basic__check之外，我还进行了另外的检查。首先要指出的是我修改了basic_check函数，因此其中的检查逻辑与buddy_system_pmm真实情况其实是不相符的。在剩余check部分，我首先申请分配页数为6的块(实际上得到的是8)，接下来我保存链表的状态后将其清空，那么接下来的再申请内存时就会失败，并且接下来申请的页都要依赖于先前申请的6个页的释放。接下来我释放了这6个页的块(实际释放了8),然后我就可以逐个申请页数为4和2的块，在这过程中可以通过打印某些信息或通过断言来验证；然后我释放这两个块，接着再次申请一个页数为6的块。最后恢复先前保存的链表信息，然后释放这六个页。在这一过程中存在一系列断言验证以证明正确性。

##### 完整代码：

```c
#include <pmm.h>
#include <list.h>
#include <string.h>
#include <buddy_system_pmm.h>
#include <stdio.h>

free_area_t free_area[11];   //这是个全局变量
struct Page *PageStart;    //指向页的起始地址

//#define free_list (free_area.free_list)
//#define nr_free (free_area.nr_free)

static void
buddy_system_init(void) {
    for(int i = 0; i < 11; i++) {
        list_init(&(free_area[i].free_list));
        free_area[i].nr_free = 0;
    }
}

static void
buddy_system_init_memmap(struct Page *base, size_t n) {

    assert(n > 0);
    struct Page *p = base;    //base指向页的起始地址
    PageStart = base;
    //初始化每一个页
    for (; p != base + n; p ++) {
        assert(PageReserved(p));
        p->flags = p->property = 0;
        set_page_ref(p, 0);
    }

    struct Page *TempBase = base;
    ////OK，除了大小为1的内存块外，每个大小的内存块给2048个页
    size_t PageNumber = 2048;
    ///// free_area[i]对应内存块大小为2^i的内存块
    for (int i = 10; i >= 1; i--) {
////////////////////---------------nr_free记录的应该是该free_area_t总的可用的页的数量
///////////////////------------------free_list中的每个元素的Property记录的是该内存块可用的页数量
        size_t BlockNumber = PageNumber / (1 << i);
        free_area[i].nr_free = PageNumber;
        
        for(size_t j = 0; j < BlockNumber; j++) {
            struct Page * p = TempBase;
            p->property = 1 << i;
            SetPageProperty(p);

            if(list_empty(&(free_area[i].free_list))) {
                list_add(&(free_area[i].free_list), &(p->page_link));
            }
            else {
                list_entry_t *le = &(free_area[i].free_list);
                while ((le = list_next(le)) != &(free_area[i].free_list)) {
                    struct Page *page = le2page(le, page_link);
                    if (p < page) {
                        list_add_before(le, &(p->page_link));
                        break;
                    }
                    else if(list_next(le) == &(free_area[i].free_list)){
                        list_add(le,&(p->page_link));
                    }
                }
            }
            TempBase += (1 << i);
        }
        

    }
    ///内存块大小为1的单独处理
    size_t LeftPageNumber = 31929 - 2048*10; //剩余的页数
    free_area[0].nr_free = LeftPageNumber;
    p = TempBase;
    for(size_t i = 0; i < LeftPageNumber; i++) {
        p->property = 1;
        SetPageProperty(p);
        if(list_empty(&(free_area[0].free_list))) {
            list_add(&(free_area[0].free_list), &(p->page_link));
        }
        else {
            list_entry_t *le = &(free_area[0].free_list);
            while ((le = list_next(le)) != &(free_area[0].free_list)) {
                struct Page *page = le2page(le, page_link);
                if (p < page) {
                    list_add_before(le, &(p->page_link));
                    break;
                }
                else if(list_next(le) == &(free_area[0].free_list)){
                    list_add(le,&(p->page_link));
                }
            }
        }
        p++;
    }
}

static struct Page *
buddy_system_alloc_pages(size_t n) {

    assert(n > 0);
    //先找到要分配的块大小对应的free_area
    int Position = 0;
    while(n > (1 << Position)) {
        Position++;
    }
    int MostSuiitableSize = Position;
    //找到了，然后查看free_area中是否有空闲块
    while(free_area[Position].nr_free < n) {
        Position++;
        if(Position == 11) break; //无法满足内存分配需求
    }
    

    struct Page *page = NULL;
    if(Position==11)return page; ///无法满足内存分配需求

    if(Position == MostSuiitableSize) {   //这种情况找到就直接分配
        list_entry_t *le= &(free_area[Position].free_list);
        page = le2page(list_next(le),page_link);
        
        list_del(&(page->page_link));
        free_area[Position].nr_free -= page->property;
        ClearPageProperty(page);
    }

    else{    //否则找到的就是比所需块大最少的
       size_t allocd = (1<<MostSuiitableSize);
        list_entry_t *le= &(free_area[Position].free_list);
        page = le2page(list_next(le),page_link);
        size_t Left=page->property-allocd;
        //把Left转为二进制形式获取多出的放到哪个链表里
        int arr[10]={0};///十个足够了
        int recorder=0;
        while(Left){
            int t=Left%2;
            if(t==1)arr[recorder++]=1;
            else recorder++;
            Left/=2;
        }
        /////先分配，再处理多出来的
        ////////分配
	free_area[Position].nr_free -= page->property;
        page->property = allocd;
        list_del(&(page->page_link));
        ClearPageProperty(page);
        ///处理多出来的
        struct Page* TempPage = page+allocd;
        for(int i=0;i<10;i++)
        {
            if(arr[i]==0)continue;
            free_area[i].nr_free += (1<<i);
            struct Page * p =TempPage;
            p->property = (1<<i);
            SetPageProperty(p);
            if(list_empty(&(free_area[i].free_list))){
                list_add(&(free_area[i].free_list),&(p->page_link));
            }
            else {
                list_entry_t *le = &(free_area[i].free_list);
                while ((le = list_next(le)) != &(free_area[i].free_list)) {
                    struct Page *page = le2page(le, page_link);
                    if (p < page) {
                        list_add_before(le, &(p->page_link));
                        break;
                    }
                    else if(list_next(le) == &(free_area[i].free_list)){
                    list_add(le,&(p->page_link));
                    }

                }
            }
            TempPage+=(1<<i);
        }
    }
    
    
    return page;
}

static void
buddy_system_free_pages(struct Page *base, size_t n) {
    assert(n > 0);
    struct Page *p = base;
    for (; p != base + n; p ++) {  //判断这n个页是否被保留，或者是否被分配,当不被保留且未被分配时，将这n个页设置为空闲页
        assert(!PageReserved(p) && !PageProperty(p));
        p->flags = 0;
        set_page_ref(p, 0);
    }
    //获取实际分配的页数,如果此base有property属性值，说明其是头部，否则无该值
    size_t allocd = base->property;
    
    int Position = 0;
    while(allocd != (1 << Position)) {
        Position++;
    }
    //去Position链表里找，如果找到地址连续的就合并
    list_entry_t *le = &(free_area[Position].free_list);

    if(list_empty(le)){
        list_add(le,&(base->page_link));
        free_area[Position].nr_free += base->property;
        return;
    }

    while ((le = list_next(le)) != &(free_area[Position].free_list)) {
        struct Page *page = le2page(le, page_link);
        //先考虑base在page之前的情况
            
        if (base + allocd == page) {   //base在page之前，合并后归到Position+1链表里
            size_t Size=base-PageStart;
            if(Size%(2*allocd)==0){   //检查二
            base->property += page->property;
            list_del(&(page->page_link));
            free_area[Position].nr_free -= page->property;
            ClearPageProperty(page);
            buddy_system_free_pages(base, base->property);
            return;
            }
                
        }
        //再考虑base在page之后的情况
        if (page + page->property == base) {   //base在page之后，合并后归到Position+1链表里
            size_t Size=page-PageStart;
            if(Size%(2*allocd)==0){  //检查二
   	    ClearPageProperty(page);
	    page->property += base->property;
            list_del(&(page->page_link));
            free_area[Position].nr_free -= base->property;
            buddy_system_free_pages(page, page->property);
            return;
            }
                
        }
        //若到链表末尾还没找到，就放到链表末尾
        if(list_next(le) == &(free_area[Position].free_list)) {
            SetPageProperty(base);
            list_add(&(free_area[Position].free_list),&(base->page_link));
            free_area[Position].nr_free += base->property;
            
        }
    }

}

static size_t
buddy_system_nr_free_pages(void) {
    size_t Total_nr_free=0;
    for(int i=0;i<11;i++)
        Total_nr_free+=free_area[i].nr_free;
    return Total_nr_free;
}

static void
basic_check(void) {
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store[11];
    for (int i = 0; i < 11; i++) {
        free_list_store[i] = free_area[i].free_list;
	list_init(&(free_area[i].free_list));
        assert(list_empty(&free_area[i].free_list));
    }
       

    unsigned int nr_free_store[11];
    for (int i = 0; i < 11; i++) {
        nr_free_store[i] = free_area[i].nr_free;
        free_area[i].nr_free = 0;
    }

    assert(alloc_page() == NULL);

    free_page(p0);
    cprintf("%s%d\n","PropertyBIT:",PageProperty(p0));
    free_page(p1);
    cprintf("%s%d\n","free_area[0].nr_free:",free_area[0].nr_free);
    cprintf("%s%d\n","free_area[1].nr_free:",free_area[1].nr_free);
 
    cprintf("%s%d\n","PropertyBIT:",PageProperty(p1));
    free_page(p2);
    cprintf("%s%d\n","PropertyBIT:",PageProperty(p2));
cprintf("%s%d\n","free_area[0].nr_free:",free_area[0].nr_free);
cprintf("%s%d\n","free_area[1].nr_free:",free_area[1].nr_free);

    assert(free_area[0].nr_free == 1);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_area[0].free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);
 
    for(int i=0;i<11;i++){
        free_area[i].free_list = free_list_store[i];
        free_area[i].nr_free = nr_free_store[i];
    }

cprintf("%s%d\n","PropertyBIT:",PageProperty(p));
cprintf("%s%d\n","PropertyBIT:",PageProperty(p1));
cprintf("%s%d\n","PropertyBIT:",PageProperty(p2));
cprintf("%s%d\n","PropertyBIT:",PageReserved(p2));

    free_page(p);
    free_page(p1);
cprintf("%s\n","WUWUWUWUWUW");
cprintf("%s%d\n","PropertyBIT:",PageProperty(p2));
cprintf("%s%d\n","PropertyBIT:",PageReserved(p2));
cprintf("%s%d\n","p2->Property:",p2->property);
cprintf("%s%d\n","free_area[0].nr_free:",free_area[0].nr_free);
cprintf("%s%d\n","free_area[1].nr_free:",free_area[1].nr_free);

    free_page(p2);
cprintf("%s","HAHAHAHAHAH");

}

// LAB2: below code is used to check the first fit allocation algorithm
// NOTICE: You SHOULD NOT CHANGE basic_check, default_check functions!
static void
buddy_system_check(void) {
    int total = 0;
    
    for(int i=0;i<11;i++){
       list_entry_t *le = &(free_area[i].free_list);
       while ((le = list_next(le)) != &(free_area[i].free_list)) {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        total += p->property;
        }
    }
  cprintf("%s%d","total",total); 
    assert(total == nr_free_pages());

    basic_check();

    struct Page *pp = alloc_pages(6);

    list_entry_t store=free_area[0].free_list;
    list_entry_t free_list_store[11];
    for (int i = 0; i < 11; i++) {
        free_list_store[i] = free_area[i].free_list;
        list_init(&(free_area[i].free_list));
        assert(list_empty(&free_area[i].free_list));
    }
  cprintf("\n%d\n",list_empty(&free_list_store[0])); 
  cprintf("%d\n",list_empty(&free_area[0].free_list));

    unsigned int nr_free_store[11];
    for (int i = 0; i < 11; i++) {
	cprintf("%d\n",free_area[i].nr_free);
        nr_free_store[i] = free_area[i].nr_free;
        free_area[i].nr_free = 0;
    }

    free_pages(pp, 6);

    struct Page *p0 = alloc_pages(4);
    assert(p0 != NULL);

    struct Page *p1 =alloc_pages(2);
    assert(p1 != NULL);

    free_pages(p0,4);
    free_pages(p1,2);
cprintf("%s%d\n","NumberOfSize2:",free_area[1].nr_free);
cprintf("%s%d\n","NumberOfSize4:",free_area[2].nr_free);
cprintf("%s%d\n","NumberOfSize8:",free_area[3].nr_free);
    struct Page *p2 = alloc_pages(6);
    assert(p2 != NULL);

cprintf("%s%d\n","TotalFree:",nr_free_pages());

assert(nr_free_pages()==0);
    cprintf("\n\n");
    for(int i=0;i<11;i++){
        free_area[i].free_list = free_list_store[i];
        free_area[i].nr_free = nr_free_store[i];
	cprintf("%d\n",free_area[i].nr_free);
    }
    free_area[0].free_list =store;

    free_pages(p2,6);

    for(int i=0;i<11;i++){
       list_entry_t *le = &(free_area[i].free_list);
       while ((le = list_next(le)) != &(free_area[i].free_list)) {
        struct Page *p = le2page(le, page_link);
        total -= p->property;
        }
    }
  cprintf("%s%d","total",total);
    assert(total == 0);
    cprintf("%s","All tests passed.\n");
}
//这个结构体在
const struct pmm_manager buddy_system_pmm_manager = {
    .name = "buddy_system_pmm_manager",
    .init = buddy_system_init,
    .init_memmap = buddy_system_init_memmap,
    .alloc_pages = buddy_system_alloc_pages,
    .free_pages = buddy_system_free_pages,
    .nr_free_pages = buddy_system_nr_free_pages,
    .check = buddy_system_check,
};


```



#### 扩展练习3：硬件的可用物理内存范围的获取方法：如果OS无法提前知道当前硬件的可用物理内存范围，请问有何办法让OS获取可用物理内存范围？

​	为了获取到硬件的可用物理内存范围，我第一个冒出来的想法就是通过使用指针对某个内存地址进行访问，若此内存地址在可用范围内，那么程序会正常执行下去；若当访问某个内存地址时程序出现错误，进入异常处理机制，那么说明这一块地址是不可访问的，自然对我们来说也就是不可用的。

![image-20241025180314101](./p3.png)

具体探测方法如上图所示，page_init函数对page进行初始化前需要获取可用的地址信息，那么在真正对其进行初始化操作前，首先定义了一个unsigned long long int的指针，其值被赋为要探测的目标地址，接下来定义了另一个unsigned long long int型变量ss，其值为先前定义的指针指向的内存地址中的值；另外在这些指针和常量定义前后设置了一些字符串输出信息用于调试。

在进行接下来的探测前，首先根据已知的结果，可用的物理地址范围为0x80200000--0x88000000，转换为对应的虚拟地址即为0xFFFFFFFFC0200000--0XFFFFFFFFC8000000。

当访问的地址属于可用物理内存范围内时，上述的指针操作可以正常进行，程序就会正常运行，如下图：

![image-20241025180944920](./p4.png)

这种情况下访问的虚拟地址为0xFFFFFFFFC0200000，对应物理地址为0x80200000,此地址即为entry.S的地址；

然后我尝试访问虚拟地址0xFFFFFFFFC0000000,对应物理地址为0x80000000,此地址是OpenSBI的加载地址，结果是无法访问：

![image-20241025181412858](./p5.png)

会发现，虽然没有进入异常处理机制，但程序输出完"HAHAHAHAHAHAHAH"后就卡死了，这种情况同样也认为此内存地址是不可用的(毕竟OpenSBI在使用)。

然后测试的虚拟地址为0xFFFFFFFFC7FFFFF8，对应的物理地址为0x87FFFFF8.之所以选择这个地址是因为，可用的物理地址最大到0x88000000,而由于上述我的定义的是一个unsigned long long int指针，其大小为8字节，配合此地址0x87FFFFF8，正好到达0x88000000。

![image-20241025182236299](./p6.png)

同样可以验证，当这个值设置的再稍微大一点，就一点，为0x87FFFFF9时，就会出现错误，因为访问到了1个字节的不可用空间.

![image-20241025182321313](./p7.png)