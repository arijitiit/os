#include "threads/malloc.h"
#include <debug.h>
#include <list.h>
#include <round.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "threads/palloc.h"
#include "threads/synch.h"
#include "threads/vaddr.h"

struct desc
  {
    size_t block_size;          /* Size of each element in bytes. */
    struct list free_list;      /* List of free blocks. */
    struct lock lock;           /* Lock. */
  };

struct block 
  {
    struct list_elem free_elem; /* Free list element. */
    size_t block_size;
  };

static struct lock page_lock;

static struct desc descs[10];   /* Descriptors. */
static size_t desc_cnt;         /* Number of descriptors. */
static size_t count;         /* Number of pages. */
static void *page_header[100];

// void printMemory();

void printMemory(){
  struct list_elem *e;

  struct desc *d;
  void *a;

  printf("Print Memory\nCount: %d\n", count);
  for(unsigned int i=1; i<=count; i++)
  {
    printf("Page No:- %d\n",i);
    for (d = descs; d < descs + desc_cnt; d++)
    {
      printf("Descriptor %4d:  ",d->block_size);
      for (e = list_begin (&d->free_list); e != list_end (&d->free_list); e = list_next (e))
      {
        struct block *b=list_entry(e,struct block, free_elem);
        a = (void *)pg_round_down(b);
        if(a==page_header[i])
          printf("%10p   ",e);
      }
      printf("\n");
    }
    printf("\n");
  }
}

void
malloc_init (void) 
{
  size_t block_size;
  count = 0;
  lock_init (&page_lock);
  for (block_size = 16; block_size <= PGSIZE / 2; block_size *= 2)
    {
      struct desc *d = &descs[desc_cnt++];
      ASSERT (desc_cnt <= sizeof descs / sizeof *descs);
      d->block_size = block_size;
      list_init (&d->free_list);
      lock_init (&d->lock);
    }
}

void *
malloc (size_t size) 
{
  struct desc *d,*d_temp;
  struct list_elem *e;
  struct block *b;
  struct desc *temp;
  size_t par_size;

  printf("Allocating a block of size: %4d\n",size);

  if (size == 0)
    return NULL;

  size += sizeof(struct block);
  for (d = descs; d < descs + desc_cnt; d++)
    if (d->block_size >= size)
      break;
  if (d == descs + desc_cnt) 
    {
      size_t page_cnt = DIV_ROUND_UP (size + sizeof *b, PGSIZE);
      b = palloc_get_multiple (0, page_cnt);
      if (b == NULL)
        return NULL;
      b->block_size = page_cnt*PGSIZE;
      printf("Number of pages allocated: %4d\n",page_cnt);
      return b + 1;
    }

  lock_acquire (&d->lock);
  // d_temp = d;
  for(d_temp = d;d_temp != descs + desc_cnt && list_empty(&d_temp->free_list);d_temp++){
    lock_release (&d_temp->lock);
    // d_temp++;
    if(d_temp + 1 != descs + desc_cnt)
      lock_acquire (&(d_temp + 1)->lock);
  }
  
  if (d_temp == descs + desc_cnt){
    b = palloc_get_page (0);
    if (b == NULL) 
      {
        return NULL; 
      }
    lock_acquire(&page_lock);
    count++;
    page_header[count] = b;
    lock_release(&page_lock);
    par_size = PGSIZE;
    temp = d_temp;
    struct block* free_b;

    while(d->block_size != par_size){
      lock_acquire (&(temp-1)->lock);
      free_b = (struct block *) ((uint8_t *) b + par_size/2);
      free_b->block_size = par_size/2;
      for (e = list_begin (&(temp-1)->free_list); e != list_end (&(temp-1)->free_list); e = list_next (e))
      if (&free_b->free_elem < e)
        break;
      list_insert (e, &free_b->free_elem);

      lock_release (&(temp-1)->lock);
      temp--; par_size /= 2;
    }

  }else{
    b = list_entry (list_pop_front (&d_temp->free_list), struct block, free_elem);
    if(d_temp != d){
      par_size = d_temp->block_size;
      temp = d_temp;
      struct block* free_b;

      while(d->block_size != par_size){
        lock_acquire (&(temp-1)->lock);
        free_b = (struct block *) ((uint8_t *) b + par_size/2);
        free_b->block_size = par_size/2;
        for (e = list_begin (&(temp-1)->free_list); e != list_end (&(temp-1)->free_list); e = list_next (e))
        if (&free_b->free_elem < e)
          break;
        list_insert (e, &free_b->free_elem);

        lock_release (&(temp-1)->lock);
        temp--; par_size /= 2;
      }
    }
  }
  if(d_temp != descs + desc_cnt)
    lock_release (&d_temp->lock);
  b->block_size = d->block_size;
  printf("Size allocated: %4d\n", b->block_size);
  return b + 1;
} 

void *
calloc (size_t a, size_t b) 
{
  void *p;
  size_t size;

  size = a * b;
  if (size < a || size < b)
    return NULL;

  p = malloc (size);
  if (p != NULL)
    memset (p, 0, size);

  return p;
}

void *
realloc (void *old_block, size_t new_size) 
{
  // printf("Realloc\n");
  if (new_size == 0) 
    {
      free (old_block);
      return NULL;
    }
  else 
    {
      void *new_block = malloc (new_size);
      if (old_block != NULL && new_block != NULL)
        {
          struct block* old = old_block;
          old--;
          size_t old_size = old->block_size;
          size_t min_size = new_size < old_size ? new_size : old_size;
          printf("Reallcating %4d with %4d block\n",old_size, new_size);          
          memcpy (new_block, old_block, min_size);
          free (old_block);
        }
      return new_block;
    }
}

void
free (void *p) 
{
  size_t par_size;
  struct desc *d_temp;
  bool flag = true;

  if (p != NULL)
    {
      struct block *b = p;
      b -= 1;
      size_t blksize = b->block_size;
      if (blksize >= PGSIZE){
        printf("Number of pages freed: %4d\n",blksize/PGSIZE);
        palloc_free_multiple (b, blksize/PGSIZE);
        return;
      } 
      else {
          struct desc *d;
          for(d = descs; d < descs + desc_cnt; d++)
            if(d->block_size == blksize)
              break;
          lock_acquire (&d->lock);
          par_size = PGSIZE;
          d_temp = d;

          while(b->block_size < par_size && flag){
            flag = false;
            size_t size = b->block_size;
            struct block* buddy = NULL;
            struct list_elem* e;
            e = list_begin (&d_temp->free_list);
            while(e != list_end (&d_temp->free_list)){
              buddy = list_entry (e, struct block, free_elem);
              size_t xor = (size_t)((uintptr_t)buddy^((uintptr_t)b));
              if(xor == size){
                break;
              }
              e = list_next (e);
            }
            if(e == list_end(&d_temp->free_list)){
              struct list_elem *e;
              for (e = list_begin (&d_temp->free_list); e != list_end (&d_temp->free_list); e = list_next (e))
                if (&b->free_elem < e)
                  break;
              list_insert (e, &b->free_elem);
            }else{
              printf("Buddy found for %4d sized block\n",size);
              list_remove(&buddy->free_elem);
              if(buddy < b)
                b = buddy;
              if(size < par_size/2){
                b->block_size = 2*size;
                d_temp++;
                flag = true;
              }else{
                lock_acquire(&page_lock);
                count--;
                lock_release(&page_lock);
                printf("Page Freed\n");
                palloc_free_page(b);
              }
            }
          }                   
          lock_release (&d->lock);
        }
    }
}