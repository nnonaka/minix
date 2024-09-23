#ifndef _MTHREAD_H
#define _MTHREAD_H

#include <minix/config.h>	/* MUST be first */
#include <minix/const.h>
#include <sys/types.h>
#include <stdio.h>
#include <ucontext.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/signal.h>

#include <minix/mthread_types.h>



__BEGIN_DECLS
/* allocate.c */
int mthread_create(mthread_thread_t *thread, mthread_attr_t *tattr, void
	*(*proc)(void *), void *arg);
int mthread_detach(mthread_thread_t thread);
int mthread_equal(mthread_thread_t l, mthread_thread_t r);
void mthread_exit(void *value);
int mthread_join(mthread_thread_t thread, void **value);
int mthread_once(mthread_once_t *once, void (*proc)(void));
mthread_thread_t mthread_self(void);

/* attribute.c */
int mthread_attr_destroy(mthread_attr_t *tattr);
int mthread_attr_getdetachstate(mthread_attr_t *tattr, int
	*detachstate);
int mthread_attr_getstack(mthread_attr_t *tattr, void **stackaddr,
	size_t *stacksize);
int mthread_attr_getstacksize(mthread_attr_t *tattr, size_t *stacksize);
int mthread_attr_init(mthread_attr_t *tattr);
int mthread_attr_setdetachstate(mthread_attr_t *tattr, int detachstate);
int mthread_attr_setstack(mthread_attr_t *tattr, void *stackaddr, size_t
	stacksize);
int mthread_attr_setstacksize(mthread_attr_t *tattr, size_t stacksize);


/* condition.c */
int mthread_cond_broadcast(mthread_cond_t *cond);
int mthread_cond_destroy(mthread_cond_t *cond);
int mthread_cond_init(mthread_cond_t *cond, mthread_condattr_t *cattr);
int mthread_cond_signal(mthread_cond_t *cond);
int mthread_cond_wait(mthread_cond_t *cond, mthread_mutex_t *mutex);

/* key.c */
int mthread_key_create(mthread_key_t *key, void (*destructor)(void *));
int mthread_key_delete(mthread_key_t key);
void *mthread_getspecific(mthread_key_t key);
int mthread_setspecific(mthread_key_t key, void *value);

/* misc.c */
void mthread_stats(void);
void mthread_verify_f(char *f, int l);
#define mthread_verify() mthread_verify_f(__FILE__, __LINE__)
void mthread_stacktrace(mthread_thread_t t);
void mthread_stacktraces(void);

/* mutex.c */
int mthread_mutex_destroy(mthread_mutex_t *mutex);
int mthread_mutex_init(mthread_mutex_t *mutex, mthread_mutexattr_t
	*mattr);
int mthread_mutex_lock(mthread_mutex_t *mutex);
int mthread_mutex_trylock(mthread_mutex_t *mutex);
int mthread_mutex_unlock(mthread_mutex_t *mutex);

/* event.c */
int mthread_event_destroy(mthread_event_t *event);
int mthread_event_init(mthread_event_t *event);
int mthread_event_wait(mthread_event_t *event);
int mthread_event_fire(mthread_event_t *event);
int mthread_event_fire_all(mthread_event_t *event);

/* rwlock.c */
int mthread_rwlock_destroy(mthread_rwlock_t *rwlock);
int mthread_rwlock_init(mthread_rwlock_t *rwlock);
int mthread_rwlock_rdlock(mthread_rwlock_t *rwlock);
int mthread_rwlock_wrlock(mthread_rwlock_t *rwlock);
int mthread_rwlock_unlock(mthread_rwlock_t *rwlock);

/* schedule.c */
int mthread_yield(void);
void mthread_yield_all(void);
__END_DECLS

#if defined(_MTHREADIFY_PTHREADS)

__BEGIN_DECLS
/* allocate.c */
int pthread_create(pthread_t *thread, pthread_attr_t *tattr, void
	*(*proc)(void *), void *arg);
int pthread_detach(pthread_t thread);
int pthread_equal(pthread_t l, pthread_t r);
void pthread_exit(void *value);
int pthread_join(pthread_t thread, void **value);
int pthread_once(pthread_once_t *once, void (*proc)(void));
pthread_t pthread_self(void);

/* attribute.c */
int pthread_attr_destroy(pthread_attr_t *tattr);
int pthread_attr_getdetachstate(pthread_attr_t *tattr, int
	*detachstate);
int pthread_attr_getstack(pthread_attr_t *tattr, void **stackaddr,
	size_t *stacksize);
int pthread_attr_getstacksize(pthread_attr_t *tattr, size_t *stacksize);
int pthread_attr_init(pthread_attr_t *tattr);
int pthread_attr_setdetachstate(pthread_attr_t *tattr, int detachstate);
int pthread_attr_setstack(pthread_attr_t *tattr, void *stackaddr, size_t
	stacksize);
int pthread_attr_setstacksize(pthread_attr_t *tattr, size_t stacksize);

/* condition.c */
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_init(pthread_cond_t *cond, pthread_condattr_t *cattr);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);

/* key.c */
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, void *value);

/* mutex.c */
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_init(pthread_mutex_t *mutex, pthread_mutexattr_t
	*mattr);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* event.c */
int pthread_event_destroy(pthread_event_t *event);
int pthread_event_init(pthread_event_t *event);
int pthread_event_wait(pthread_event_t *event);
int pthread_event_fire(pthread_event_t *event);
int pthread_event_fire_all(pthread_event_t *event);

/* rwlock.c */
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_init(pthread_rwlock_t *rwlock,
	pthread_rwlockattr_t *attr);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

/* pthread_compat.c */
int pthread_spin_destroy(pthread_spinlock_t *lock);
int pthread_spin_init(pthread_spinlock_t *lock, int pshared);
int pthread_spin_lock(pthread_spinlock_t *lock);
int pthread_spin_trylock(pthread_spinlock_t *lock);
int pthread_spin_unlock(pthread_spinlock_t *lock);

/* schedule.c */
int pthread_yield(void);
int sched_yield(void);
void pthread_yield_all(void);

/* LSC: FIXME: Maybe we should really do something with those... */
#define pthread_mutexattr_init(u) (0)
#define pthread_mutexattr_destroy(u) (0)

#define PTHREAD_MUTEX_RECURSIVE 0
#define pthread_mutexattr_settype(x, y) (EINVAL)
__END_DECLS

#endif /* defined(_MTHREADIFY_PTHREADS) */
#endif
