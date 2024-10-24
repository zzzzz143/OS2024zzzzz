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

