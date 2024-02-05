/*
 * mm_alloc.h
 *
 * Exports a clone of the interface documented in "man 3 malloc".
 */

#pragma once

#ifndef _malloc_H_
#define _malloc_H_

 /* Define the block size since the sizeof will be wrong */
#define BLOCK_SIZE 40

#ifdef __cplusplus
extern "C" {
#endif

#include <unistd.h>
#include <stdlib.h>
#include <string.h> 

typedef struct s_block *s_block_ptr;

struct s_block {
    size_t size;
    struct s_block *next;
    struct s_block *prev;
    int free;
    void *ptr;
    /* A pointer to the allocated block */
    char data [0];
 };

void* mm_malloc(size_t size);
void* mm_realloc(void* ptr, size_t size);
void mm_free(void* ptr);

extern s_block_ptr base;

s_block_ptr find_block(s_block_ptr *last, size_t size);
void split_block(s_block_ptr b, size_t size);
s_block_ptr fusion(s_block_ptr b);
s_block_ptr get_block(void *p);
s_block_ptr extend_heap(s_block_ptr last, size_t size);


#ifdef __cplusplus
}
#endif

#endif