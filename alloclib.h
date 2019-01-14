/*
 * Implements _malloc library in C.
 * This _malloc implemtation has per thread bins.
 * bins are according to four sizes:
 * 8 bytes, 64 bytes, 512 bytes and greated than 512 bytes.
 *
 * memory for size > 512 bytes is allocated through mmap system call.
 * memory blocks are initialized lazily and are appended on free list after
 * free() call.
 *
 */

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <mcheck.h>

//#ifndef MALLOC_H
#define MALLOC_H 1


typedef struct block_info
{
   int size;
   struct block_info *next;
}block_info;

pthread_mutex_t global_heap_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t stats_mutex = PTHREAD_MUTEX_INITIALIZER;

unsigned long total_arena_size_allocated = 0;

unsigned long total_mmap_size_allocated = 0;

unsigned long total_number_of_blocks = 0;

unsigned long total_allocation_request = 0;

unsigned long total_free_request = 0;

unsigned long total_free_blocks = 0;


/* Pentru bucati de 8, 64, 512, sau mai multi bytes.
 *
 * Each thread will have its own bin and storage arena.
 * Iniially all the bins are empty. The list gets build up on successive free
 * calls after _malloc.

 Fiecare proces va avea propria zona de memorie.
 Initial fiecare zona e goala.
 */
__thread block_info *bin_8     = NULL;
__thread block_info *bin_64    = NULL;
__thread block_info *bin_512   = NULL;
__thread block_info *bin_large = NULL;


void *heap_used_memory_end = NULL;

//pointer catre o zona din heap care nu a folst folosita inca
__thread void *thread_unused_heap_start = NULL;


// / capatul zonei de memorie heap a thread-ului
__thread void *thread_heap_end = NULL;




void *align8(void *x)
{
    unsigned long p = (unsigned long)x;
    p = (((p - 1) >> 3) << 3) + 8;
    return (void *)p;
}

block_info** get_bin(size_t size)
{
    switch(size)
    {
       case 8   : return &bin_8;
       case 64  : return &bin_64;
       case 512 : return &bin_512;
       default  : return &bin_large;
    }
}







/*
 * Finds best fit block from bin_large. On memory request > 512, first
 * it is checked if any of large free memory chunks fits to request.
 *
 * params: size of block to allocate.
 * returns: pointer to best fitting block, NULL on failure.
 */

void * find_best_fit_from_bin_large(size_t size)
{
    block_info *b = bin_large;
    block_info *best_fit = NULL;
    int min_fit_size = INT_MAX;
    void *ret = NULL;

    while(b != NULL)
    {
         if(b->size >= size && b->size < min_fit_size)
         {
            best_fit = b;
            min_fit_size = b->size;
         }
         b = b->next;
    }

    if(NULL != best_fit)
    {
        if (best_fit == bin_large)
        {
           bin_large = bin_large->next;
           best_fit->next = NULL;
           ret = (void *)((void *)best_fit + sizeof(block_info));
        }
        else
        {
          b = bin_large;
          while(b != NULL && b->next != best_fit)
          {
            b = b->next;
          }
          if(b != NULL)
          {
             b->next = best_fit->next;
          }
          best_fit->next = NULL;
          ret = (void *)((void *)best_fit + sizeof(block_info));
        }
    }

    return ret;
}

void * mmap_new_memory(size_t size)
{
    int num_pages =
        ((size + sizeof(block_info) - 1)/sysconf(_SC_PAGESIZE)) + 1;
    int required_page_size = sysconf(_SC_PAGESIZE) * num_pages;

    void *ret = mmap(NULL,
                     required_page_size,
                     PROT_READ | PROT_WRITE,
                     MAP_ANONYMOUS| MAP_PRIVATE,
                     -1,
                     0);

    block_info b;
    b.size = (required_page_size - sizeof(block_info));
    b.next = NULL;

    ret = memcpy(ret, &b, sizeof(block_info));
    ret = ((char*)ret + sizeof(block_info));

    pthread_mutex_lock(&stats_mutex);
    total_mmap_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return ret;
}

void *alloc_large(size_t size)
{
   void * ret = NULL;
   if(NULL != bin_large)
   {
       ret = find_best_fit_from_bin_large(size);
   }

   if(ret == NULL)
   {
       ret = mmap_new_memory(size);
   }
    return ret;
}

void * block_from_unused_heap(size_t size)
{
    if(NULL == thread_unused_heap_start ||
       (thread_heap_end - thread_unused_heap_start) <
           (size + sizeof(block_info)))
    {
        if(NULL == heap_used_memory_end ||
            (sbrk(0) - heap_used_memory_end) < (size + sizeof(block_info)))
        {
            if(NULL == heap_used_memory_end)
            {
                heap_used_memory_end = sbrk(0);
                if(heap_used_memory_end == (void*) -1)
                {
                    errno = ENOMEM;
                    perror("\n sbrk(0) failed.");
                    return NULL;
                }
            }

            align8(heap_used_memory_end);
        }

        if(sbrk(sysconf(_SC_PAGESIZE) * 100) == (void *) -1)
        {
            errno = ENOMEM;
            perror("\n sbrk failed to extend heap.");
            return NULL;
        }

        thread_unused_heap_start = heap_used_memory_end;
        thread_heap_end =
            heap_used_memory_end + (sysconf(_SC_PAGESIZE));
        heap_used_memory_end =  thread_heap_end;
    }

    block_info b;
    b.size = size;
    b.next = NULL;

    memcpy(thread_unused_heap_start, &b, sizeof(block_info));
    thread_unused_heap_start += (sizeof(block_info) + size);

    pthread_mutex_lock(&stats_mutex);
    total_number_of_blocks++;
    total_arena_size_allocated += size;
    pthread_mutex_unlock(&stats_mutex);

    return (thread_unused_heap_start - size);
}


void *heap_allocate(size_t size)
{

   block_info **bin = get_bin(size);
   void * ret = NULL;
   if(NULL != *bin)
   {
       block_info *p = *bin;
       *bin =  p->next;
       p->next = NULL;

       pthread_mutex_lock(&stats_mutex);
       total_free_blocks--;
       pthread_mutex_unlock(&stats_mutex);
       ret = (void *)((char*)p + sizeof(block_info));
   }
   else
   {     pthread_mutex_lock(&global_heap_mutex);
         ret =  block_from_unused_heap(size);
         pthread_mutex_unlock(&global_heap_mutex);
   }

   return ret;
}

void* _malloc(size_t size)
{
	////

	///printf("intru in _malloc***************************\n\n\n");
	////
     pthread_mutex_lock(&stats_mutex);
     total_allocation_request++;
     pthread_mutex_unlock(&stats_mutex);

     void * ret = NULL;

     if(size < 0)
     {
        perror("\n Invalid memory request.");
        return NULL;
     }

     if(size > 512)
     {  //printf("Alloc large\n");
        ret = alloc_large(size);
     }
     else
     {
       size = (size <= 8)? 8 : ((size<=64)? 64: 512);
       ret = heap_allocate(size);
     }

     ///printf("ies din malloc\n\n\n");

     return ret;
}

void _free(void *p)
{
    ////printf("intru in functia _free *******\n\n\n");

   pthread_mutex_lock(&stats_mutex);
   total_free_request++;
   total_free_blocks++;
   pthread_mutex_unlock(&stats_mutex);

   if(NULL != p)
   {
      block_info *block  = (block_info *)(p - sizeof(block_info));
      memset(p, '\0', block->size);

      block_info **bin = get_bin(block->size);
      block_info *check_bin = *bin;

      // already freed?
      while(check_bin != NULL)
      {
         if(check_bin == block)
         {
            return;
         }
         check_bin = check_bin->next;
      }

      block->next = *bin;
      *bin = block;
    }

    ////printf("ies din functia _free\n\n\n");
}

void *_calloc(size_t nmemb, size_t size)
{
     void *p = _malloc(nmemb * size);
     block_info *b = (block_info *)(p - sizeof(block_info));
     memset(p, '\0', b->size);
     return p;
}

void *_realloc(void *ptr, size_t size)
{
   if(NULL == ptr)
   {
      return _malloc(size);
   }
   void *newptr = _malloc(size);

   if(NULL == newptr)
   {
       return NULL;
   }
   block_info *old_block =
       (block_info *)((char*)ptr - sizeof(block_info));

   memcpy(newptr, ptr, old_block->size);

   _free(ptr);

   return newptr;
}



/* aligns memory.
 */
 void *memalign(size_t alignment, size_t s)
 {
     return heap_used_memory_end;
 }


 void _malloc_stats()
 {
     printf("\n -- _malloc stats--\n");
     printf("\n total_arena_size_allocated : %lu", total_arena_size_allocated);
     printf("\n total_mmap_size_allocated  : %lu", total_mmap_size_allocated);
     printf("\n total_number_of_blocks     : %lu", total_number_of_blocks);
     printf("\n total_allocation_request   : %lu", total_allocation_request);
     printf("\n total_free_request         : %lu", total_free_request);
     printf("\n total_free_blocks          : %lu\n", total_free_blocks);
 }

void abortfn(enum mcheck_status status);
//#endif




void child_fork_handle(void)
{
   pthread_mutex_init(&global_heap_mutex, NULL);
}
void prep_fork(void)
{
    pthread_mutex_lock(&global_heap_mutex);
}

void parent_fork_handle(void)
{
  pthread_mutex_init(&global_heap_mutex, NULL);
}


__attribute__((constructor)) void sharedLibConstructor(void)
{
  int ret =
      pthread_atfork(&prep_fork,
                     &parent_fork_handle,
                     &child_fork_handle);
  if (ret != 0)
  {
      perror("pthread_atfork() error [Call #1]. _malloc is now not fork safe.");
  }
}
