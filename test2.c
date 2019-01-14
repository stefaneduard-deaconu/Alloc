#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <time.h>
#include "alloclib.h"
#include <pthread.h>


#include <errno.h>

pthread_mutex_t mtx;

void function()
{
    int i, j;
    long long *dynam[10];
    int arr[] = {1, 2, 3, 4, 7, 98, 0, 12, 35, 99, 14};
    int *table;
     //pthread_mutex_lock(&mtx);
    // critical section
    for (i = 0; i < 10; i++)
    {
        dynam[i] = _malloc(i * sizeof(long long));
        for(j=0;j<i;++j)
            dynam[i][j] = i+j;
        _realloc( dynam[i], (i*2)* sizeof(long long));
        for(j=0;j<2*i;++j)
            dynam[i][j] = i+j;
    }
    for (i = 0; i < 10; i++)
    {
        _free(dynam[i]);
    }

    for (i = 0; i < 10; i++)
    {
        printf("linia %d: ",i);
        table = _calloc(i, sizeof(int));
        for(j=0;j<i;++j)
            printf("%d",table[j]);
        printf("\n");
        _free(table);
    }
    printf("\n\n\n");
    // out of critical section
     //pthread_mutex_unlock(&mtx);
}

int main(int argc, char *argv[])
{
    function();
    int i, j, n = 1;
    pid_t pthr[10];
    //pthread_mutex_init(&mtx, NULL);
    for (i = 0; i <= n; i++)
    {
        pthr[i] = fork();
        if (pthr[i] < 0)
            return errno; // error
        else if (pthr[i] == 0)
        {
            function(); // child
            break;
        }
    }
    //
    for(i = 0; i < 10; i++)
        wait(NULL);
    printf("sfarsit");
    //
    //pthread_mutex_destroy(&mtx);
    //
    return 0;
}
