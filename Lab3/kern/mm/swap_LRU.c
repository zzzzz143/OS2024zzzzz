#include <defs.h>
#include <riscv.h>
#include <stdio.h>
#include <string.h>
#include <swap.h>
#include <swap_LRU.h>
#include <list.h>


list_entry_t pra_list_head;
/*
 * 
 */

// 初始化LRU页面替换算法
static int
_LRU_init_mm(struct mm_struct *mm)
{     
     list_init(&pra_list_head);
     mm->sm_priv = &pra_list_head;
     //cprintf(" mm->sm_priv %x in fifo_init_mm\n",mm->sm_priv);
     return 0;
}
/*
 * 
 */
static int
_LRU_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    list_entry_t *entry=&(page->pra_page_link);
 
    assert(entry != NULL && head != NULL);
    //record the page access situlation

    //(1)link the most recent arrival page at the back of the pra_list_head qeueue.
    list_add(head, entry);
    return 0;
}
/*
 *  
 */
static int
_LRU_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick)
{
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
         assert(head != NULL);
     assert(in_tick==0);
     /* Select the victim */
     //(1)  unlink the  earliest arrival page in front of pra_list_head qeueue
     //(2)  set the addr of addr of this page to ptr_page
    list_entry_t* entry = list_prev(head);
    if (entry != head) {
        list_del(entry);
        *ptr_page = le2page(entry, pra_page_link);
    } else {
        *ptr_page = NULL;
    }
    return 0;
}
/// @brief 核心思想是当访问某个页面时，将这个页面对应的page放到链表头的下一个，然后在每次分配时都分配链表头的前一个也就是链表中的最后一个
/// @param mm 
/// @param page 
/// @return 
static inline bool 
_LRU_update_list(struct mm_struct *mm, uintptr_t addr) {
    ///找到这个page，把这个page提到链表的最后即head->before
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    assert(head != NULL);
    
    list_entry_t *entry = head->next;
    while (entry != head) {
        if (PPN(le2page(entry,pra_page_link)->pra_vaddr) == PPN(addr)) {
            list_del(entry);
            list_add(head, entry);
            return true;
        }
        entry = list_next(entry);

    }
    return false;
}
static inline void
check_content_set_SUB(struct mm_struct *mm,uintptr_t addr,int value){
     *(unsigned char *)addr = value;///访问地址addr
     bool bo=_LRU_update_list(mm,addr);
     cprintf("%d\n",bo);
}
static int
_LRU_check_swap() {    
    extern struct mm_struct *check_mm_struct;  
#ifdef ucore_test
    int score = 0, totalscore = 5;
    cprintf("%d\n", &score);
    ++ score; cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==4);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==4);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==4);
    *(unsigned char *)0x2000 = 0x0b;
    ++ score; cprintf("grading %d/%d points", score, totalscore);
    assert(pgfault_num==4);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    ++ score; cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==5);
    *(unsigned char *)0x2000 = 0x0b;
    assert(pgfault_num==5);
    *(unsigned char *)0x3000 = 0x0c;
    assert(pgfault_num==5);
    ++ score; cprintf("grading %d/%d points", score, totalscore);
    *(unsigned char *)0x4000 = 0x0d;
    assert(pgfault_num==5);
    *(unsigned char *)0x5000 = 0x0e;
    assert(pgfault_num==5);
    assert(*(unsigned char *)0x1000 == 0x0a);
    *(unsigned char *)0x1000 = 0x0a;
    assert(pgfault_num==6);
    ++ score; cprintf("grading %d/%d points", score, totalscore);
#else 
    check_content_set_SUB(check_mm_struct,0x3000,0x0c);
    assert(pgfault_num==4);
    check_content_set_SUB(check_mm_struct,0x1000,0x0a);
    assert(pgfault_num==4);
    check_content_set_SUB(check_mm_struct,0x4000,0x0d);
    assert(pgfault_num==4);
    check_content_set_SUB(check_mm_struct,0x2000,0x0b);
    assert(pgfault_num==4);
    check_content_set_SUB(check_mm_struct,0x5000,0x0e);
    assert(pgfault_num==5);
    check_content_set_SUB(check_mm_struct,0x2000,0x0b);
    assert(pgfault_num==5);
    check_content_set_SUB(check_mm_struct,0x1000,0x0a);
    assert(pgfault_num==5);
    check_content_set_SUB(check_mm_struct,0x2000,0x0b);
    assert(pgfault_num==5);
    check_content_set_SUB(check_mm_struct,0x3000,0x0c);
    assert(pgfault_num==6);
    check_content_set_SUB(check_mm_struct,0x4000,0x0d);
    assert(pgfault_num==7);
    check_content_set_SUB(check_mm_struct,0x5000,0x0e);
    assert(pgfault_num==8);
    check_content_set_SUB(check_mm_struct,0x1000,0x0a);
    assert(pgfault_num==9);
#endif
    return 0;
}


static int
_LRU_init(void)
{
    return 0;
}

static int
_LRU_set_unswappable(struct mm_struct *mm, uintptr_t addr)
{
    return 0;
}

static int
_LRU_tick_event(struct mm_struct *mm)
{ return 0; }


struct swap_manager swap_manager_LRU =
{
     .name            = "LRU swap manager",
     .init            = &_LRU_init,
     .init_mm         = &_LRU_init_mm,
     .tick_event      = &_LRU_tick_event,
     .map_swappable   = &_LRU_map_swappable,
     .set_unswappable = &_LRU_set_unswappable,
     .swap_out_victim = &_LRU_swap_out_victim,
     .check_swap      = &_LRU_check_swap,
     .update_swap_cnt = &_LRU_update_list,
};
