/* A simple test harness for memory alloction. */

#include "mm_alloc.h"
#include <stdio.h>
int main(int argc, char **argv)
{
	int size = 100;
	for(int i=1; i < 100; i++){
	  void* memory = (void *)mm_malloc(size);
	  printf("%d : ", i);
	  printf("%p\n", memory);
	}

}