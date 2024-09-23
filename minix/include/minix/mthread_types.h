#ifndef _MTHREAD_TYPES_H
#define _MTHREAD_TYPES_H

/* moved from mthead.h for pthead emulation */

typedef int mthread_thread_t;
typedef int mthread_once_t;
typedef int mthread_key_t;
typedef void * mthread_condattr_t;
typedef void * mthread_mutexattr_t;

struct __mthread_tcb;
typedef struct {
  struct __mthread_tcb *mq_head;
  struct __mthread_tcb *mq_tail;
} mthread_queue_t;

struct __mthread_mutex {
  mthread_queue_t mm_queue;	/* Queue of threads blocked on this mutex */
  mthread_thread_t mm_owner;	/* Thread ID that currently owns mutex */
#ifdef MTHREAD_STRICT
  struct __mthread_mutex *mm_prev;
  struct __mthread_mutex *mm_next;
#endif
  unsigned int mm_magic;
};
typedef struct __mthread_mutex *mthread_mutex_t;

struct __mthread_cond {
  struct __mthread_mutex *mc_mutex;	/* Associate mutex with condition */
#ifdef MTHREAD_STRICT
  struct __mthread_cond *mc_prev;
  struct __mthread_cond *mc_next;
#endif
  unsigned int mc_magic;
};
typedef struct __mthread_cond *mthread_cond_t;

struct __mthread_attr {
  size_t ma_stacksize;
  char *ma_stackaddr;
  int ma_detachstate;
  struct __mthread_attr *ma_prev;
  struct __mthread_attr *ma_next;
}; 
typedef struct __mthread_attr *mthread_attr_t;

typedef struct {
  mthread_mutex_t mutex;
  mthread_cond_t cond;
} mthread_event_t;

typedef struct {
  unsigned int readers;
  mthread_thread_t writer;
  mthread_mutex_t queue;
  mthread_event_t drain;
} mthread_rwlock_t; 

/* moved from global.h */
#define MTHREAD_RND_SCHED	0	/* Enable/disable random scheduling */
#define NO_THREADS 4 
#define MAX_THREAD_POOL 1024
#define STACKSZ 4096
#define MAIN_THREAD (-1)
#define NO_THREAD (-2)
#define isokthreadid(i)	(i == MAIN_THREAD || (i >= 0 && i < no_threads))
#define MTHREAD_INIT_MAGIC 0xca11ab1e
#define MTHREAD_NOT_INUSE  0xdefec7


#define MTHREAD_CREATE_JOINABLE 001
#define MTHREAD_CREATE_DETACHED 002
#define MTHREAD_ONCE_INIT 0
#define MTHREAD_STACK_MIN MINSIGSTKSZ
#define MTHREAD_KEYS_MAX 128

#define MTHREAD_MUTEX_INITIALIZER ((mthread_mutex_t) -1)

#define MTHREAD_COND_INITIALIZER ((mthread_cond_t) -1)

#define MTHREAD_EVENT_INITIALIZER { 	MTHREAD_MUTEX_INITIALIZER,	\
										MTHREAD_COND_INITIALIZER,	\
									}

#define MTHREAD_RWLOCK_INITIALIZER { 	0,	\
										NO_THREAD,	\
										NULL,		\
										MTHREAD_EVENT_INITIALIZER,	\
									}

#if defined(_MTHREADIFY_PTHREADS)
typedef mthread_thread_t pthread_t;
typedef mthread_once_t pthread_once_t;
typedef mthread_key_t pthread_key_t;
typedef mthread_cond_t pthread_cond_t;
typedef mthread_queue_t pthread_queue_t;
typedef mthread_mutex_t pthread_mutex_t;
typedef mthread_condattr_t pthread_condattr_t;
typedef mthread_mutexattr_t pthread_mutexattr_t;
typedef mthread_attr_t pthread_attr_t;
typedef mthread_event_t pthread_event_t;
typedef mthread_rwlock_t pthread_rwlock_t;
typedef mthread_mutex_t pthread_spinlock_t; /* emulate spinlocks with mutexes */

/* LSC: No equivalent, so void* for now. */
typedef void *pthread_rwlockattr_t;

#define PTHREAD_ONCE_INIT 0
#define PTHREAD_MUTEX_INITIALIZER MTHREAD_MUTEX_INITIALIZER
#define PTHREAD_COND_INITIALIZER MTHREAD_COND_INITIALIZER
#define PTHREAD_RWLOCK_INITIALIZER MTHREAD_RWLOCK_INITIALIZER
#define PTHREAD_PROCESS_PRIVATE 0
#define PTHREAD_PROCESS_SHARED 1

/* LSC: FIXME: Maybe we should really do something with those... */
#define pthread_mutexattr_init(u) (0)
#define pthread_mutexattr_destroy(u) (0)

#define PTHREAD_MUTEX_RECURSIVE 0
#define pthread_mutexattr_settype(x, y) (EINVAL)

#endif /* defined(_MTHREADIFY_PTHREADS) */
#endif
