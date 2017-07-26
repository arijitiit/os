#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include <limits.h>
#ifdef USERPROG
#include "userprog/process.h"
#endif


#define THREAD_MAGIC 0xcd6abf4b

static struct hash tids;
static struct list all_list;

static struct list ready_list;
static struct thread *idle_thread;
struct list to_unblock_list;

static struct thread *initial_thread;

static struct lock tid_lock;

struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);
static unsigned tid_hash (const struct hash_elem *p_, void *aux UNUSED);
static bool thread_compare (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);

unsigned
tid_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct thread *p = hash_entry (p_, struct thread, hash_elem);
  return hash_bytes (&p->tid, sizeof p->tid);
}

struct thread * find_parent(const int tid){
  struct thread p;
  struct hash_elem *e;
  struct thread *t;
  
  p.tid = tid;
  e = hash_find (&tids, &p.hash_elem);
  if(e == NULL){
    return e;
  }else{
    t = hash_entry(e, struct thread, hash_elem);
    return t;    
  }
}

struct thread *
get_thread (const int tid) {
  if (tid == 1) return initial_thread;
  if (tid == 2) return idle_thread;
	return find_parent(tid);
}

bool
thread_compare (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
  const struct thread *a;
  const struct thread *b;
  a = hash_entry (a_, struct thread, hash_elem);
  b = hash_entry (b_, struct thread, hash_elem);
  if(a->tid < b->tid)return true;
  else return false;
}

void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  list_init (&ready_list);
  list_init (&to_unblock_list);
  list_init (&all_list);

  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

void
thread_start (void) 
{
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  intr_enable ();
  hash_init (&tids, tid_hash, thread_compare, NULL);
  sema_down (&idle_started);
}

void updating_cpu(void){
  struct thread *cur;
  cur = running_thread();
  if (cur->ticks > cur->lifetime) {
    if (cur->signals[3].signum == -1 && !((cur->mask >> 3) & 1)) {
      cur->signals[3].signum = 3;
      list_push_back(&(cur->signals_queue), &cur->signals[3].threadelem);
    }
  }
}

void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;
  running_thread()->ticks ++;

  updating_cpu();

  struct list_elem * e;
  e = list_begin(&ready_list);
  struct thread * T;
  
  while ( e != list_end(&ready_list) ) {
    T = list_entry(e, struct thread, elem);
    T->ticks += 1;
    if (T->ticks > T->lifetime) {
      if (T->signals[3].signum == -1){
        if(!((T->mask >> 3) & 1)) {
          T->signals[3].signum = 3;
          list_push_back(&(T->signals_queue), &T->signals[3].threadelem);
        }
      }
    } 
    e = list_next(e);
  }

  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();
}

void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;
  enum intr_level old_level;

  ASSERT (function != NULL);

  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  if(t->tid > 2)hash_insert(&tids, &t->hash_elem);
  old_level = intr_disable ();

  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  intr_set_level (old_level);

  thread_unblock (t);

  return tid;
}

void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  list_push_back (&ready_list, &t->elem);
  
  t->status = THREAD_READY;
  intr_set_level (old_level);
}

const char *
thread_name (void) 
{
  return thread_current ()->name;
}

struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

void update_chld(void){
  struct thread * par = get_thread (thread_current()->parent_tid);
  struct thread *cur;
  cur = running_thread();

  if (par) {
    unsigned short is_sig_chld = par->mask/2;

    if ((is_sig_chld & 1) == NULL) {
      if (par->signals[1].signum != -1) {
        par->signals[1].sender = cur->tid;
      }
      else {
        par->signals[1].signum = 1;
        par->signals[1].sender = cur->tid;
        list_push_back(&par->signals_queue, &par->signals[1].threadelem);
      }
    }
  }

  hash_delete(&tids, &cur->hash_elem);
}

void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  intr_disable ();
  list_remove (&thread_current()->allelem);
  update_chld();

  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  if (cur != idle_thread) 
    list_push_back (&ready_list, &cur->elem);
  cur->status = THREAD_READY;
  schedule ();
  intr_set_level (old_level);
}

void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
}

int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

int
thread_get_nice (void) 
{
  return 0;
}

int
thread_get_load_avg (void) 
{
  return 0;
}

int
thread_get_recent_cpu (void) 
{
  return 0;
}


static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      intr_disable ();
      thread_block ();

      asm volatile ("sti; hlt" : : : "memory");
    }
}

static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}


struct thread *
running_thread (void) 
{
  uint32_t *esp;

  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

static void
init_thread (struct thread *t, const char *name, int priority)
{
  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);

  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;
  t->parent_tid = running_thread()->tid;
  t->lifetime = LLONG_MAX;
  t->ticks = 0;
  running_thread()->child_total++;
  running_thread()->child_alive++;
  t->mask = 0;
  t->child_total = t->child_alive = 0;
  int i = 0;
  while ( i < 5){
    t->signals[i].signum = -1;
    i++;
  }
  list_init(&t->signals_queue);
  list_push_back (&all_list, &t->allelem);
}

static void *
alloc_frame (struct thread *t, size_t size) 
{
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  cur->status = THREAD_RUNNING;

  thread_ticks = 0;

#ifdef USERPROG
  process_activate ();
#endif

  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

void handle_unblock(void){
  struct list_elem * e;
  struct list_elem * ee;
  struct thread * s;

  e = list_begin(&to_unblock_list);
  while ( e != list_end(&to_unblock_list)) {
    ee = list_next(e);
    s = list_entry(e, struct thread, block_elem);
    list_remove(e);
    if (s->status == THREAD_BLOCKED){
      thread_unblock(s);
    }
    e = ee;
  }
}

void handle_signal(void){
  struct list_elem * e;
  struct list_elem * ee;
  struct signal_data * s;
  e = list_begin(&running_thread()->signals_queue);
  while ( e != list_end(&running_thread()->signals_queue)) {
    ee = list_next(e);
    s = list_entry(e, struct signal_data, threadelem);
    
    switch(s->signum){
      case 1:{handler(s->sender, 1); break;}
      case 2:{handler(s->sender, 2); break;}
      case 3:{handler(s->sender, 3); break;}
      case 4:{handler(s->sender, 4); break;}
    }

    list_remove(e);
    s->signum = -1;
    e = ee;
  }
}

static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();

  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);

  handle_unblock();
  
  handle_signal();
}

static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}


uint32_t thread_stack_ofs = offsetof (struct thread, stack);

void setlifetime(int x) {
  running_thread()->lifetime = x;
}