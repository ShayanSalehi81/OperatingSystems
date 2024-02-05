#include "mm_alloc.h"
#include <unistd.h>
#include <stdlib.h>
#include <string.h> 

/* Your final implementation should comment out this macro. */

s_block_ptr base = NULL;

s_block_ptr find_block(s_block_ptr *last, size_t size) {
    s_block_ptr b = base;
    while (b && !(b->free && b->size >= size)) {
        *last = b;
        b = b->next;
    }
    return b;
}

void split_block(s_block_ptr b, size_t size) {
    s_block_ptr newb;
    newb = (s_block_ptr)(b->data + size);
    newb->size = b->size - size - BLOCK_SIZE;
    newb->next = b->next;
    newb->free = 1;
    newb->ptr = newb->data;
    b->size = size;
    b->next = newb;
    if (newb->next)
        newb->next->prev = newb;
}

s_block_ptr fusion(s_block_ptr b) {
    if (b->next && b->next->free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next = b->next->next;
        if (b->next)
            b->next->prev = b;
    }
    return b;
}

s_block_ptr get_block(void *p) {
    char *tmp;
    tmp = p;
    return (p = tmp -= BLOCK_SIZE);
}


s_block_ptr extend_heap(s_block_ptr last, size_t size) {
    s_block_ptr b;
    b = (s_block_ptr)sbrk(0);
    if (sbrk(BLOCK_SIZE + size) == (void*)-1)
        return NULL;
    b->size = size;
    b->next = NULL;
    b->prev = last;
    b->ptr = b->data;
    if (last)
        last->next = b;
    b->free = 0;
    return b;
}

int valid_addr(void *p) {
    if (base) {
        if (p > base && p < sbrk(0)) {
            return p == (get_block(p))->ptr;
        }
    }
    return 0;
}

void* mm_malloc(size_t size) {
    s_block_ptr b, last;
    size_t s;
    s = size;
    if (base) {
        last = base;
        b = find_block(&last, s);
        if (b) {
            if ((b->size - s) >= (BLOCK_SIZE + 4))
                split_block(b, s);
            b->free = 0;
        } else {
            b = extend_heap(last, s);
            if (!b)
                return NULL;
        }
    } else {
        b = extend_heap(NULL, s);
        if (!b)
            return NULL;
        base = b;
    }
    return b->data;
}

void* mm_realloc(void* ptr, size_t size) {
    size_t s;
    s_block_ptr b, newb;
    void *newp;
    if (!ptr)
        return mm_malloc(size);
    if (valid_addr(ptr)) {
        s = size;
        b = get_block(ptr);
        if (b->size >= s) {
            if (b->size - s >= (BLOCK_SIZE + 4))
                split_block(b, s);
        } else {
            if (b->next && b->next->free && (b->size + BLOCK_SIZE + b->next->size) >= s) {
                fusion(b);
                if (b->size - s >= (BLOCK_SIZE + 4))
                    split_block(b, s);
            } else {
                newp = mm_malloc(s);
                if (!newp)
                    return NULL;
                newb = get_block(newp);
                memcpy(newb->ptr, b->ptr, b->size);
                mm_free(ptr);
                return newp;
            }
        }
        return ptr;
    }
    return NULL;
}

void mm_free(void* ptr) {
    s_block_ptr b;
    if (valid_addr(ptr)) {
        b = get_block(ptr);
        b->free = 1;
        if (b->prev && b->prev->free)
            b = fusion(b->prev);
        if (b->next)
            fusion(b);
        else {
            if (b->prev)
                b->prev->next = NULL;
            else
                base = NULL;
            brk(b);
        }
    }
}