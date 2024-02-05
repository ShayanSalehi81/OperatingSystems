/* A simple test harness for memory alloction. */

#include "mm_alloc.h"
#include <stdio.h>
int main(int argc, char **argv)
{
    int *data;

    data = (int*) mm_malloc(3 * sizeof(int)); 
    data[0] = 1;
    data[1] = 2;
    data[2] = 3;
    printf("data[0] = %d\n", data[0]);
    printf("data[1] = %d\n", data[1]);
    printf("data[2] = %d\n", data[2]);

    int *data2;
    data2 = (int*) mm_malloc(3 * sizeof(int));
    data2[0] = 4;
    data2[1] = 5;
    data2[2] = 6;
    printf("data2[0] = %d\n", data2[0]);
    printf("data2[1] = %d\n", data2[1]);
    printf("data2[2] = %d\n", data2[2]);
    printf("%p\n", data);
    mm_free(data);
    mm_free(data2);
    
    int * data3;
    data3 = (int*) mm_malloc(3 * sizeof(int));
    data3[0] = 7;
    data3[1] = 8;
    data3[2] = 9;
    printf("data3[0] = %d\n", data3[0]);
    printf("data3[1] = %d\n", data3[1]);
    printf("data3[2] = %d\n", data3[2]);
    printf("%p\n", data3);
    mm_free(data3);
    int* data4;
    data4 = (int*) mm_malloc(3 * sizeof(int));
    data4[0] = 10;
    data4[1] = 11;
    data4[2] = 12;
    printf("data4[0] = %d\n", data4[0]);
    printf("data4[1] = %d\n", data4[1]);
    printf("data4[2] = %d\n", data4[2]);
    printf("%p\n", data4);
    printf("all addresses should be the same!!!\n");

    printf("malloc sanity test successful!\n");
    return 0;
}