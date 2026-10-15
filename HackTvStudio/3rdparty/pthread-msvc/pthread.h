/* pthread.h - minimal POSIX threads shim for MSVC.
 *
 * Only what libhackrf's hackrf.c needs is implemented, mapped onto Win32
 * primitives (SRWLOCK, CONDITION_VARIABLE, CreateThread):
 *   pthread_mutex_{init,destroy,lock,unlock}
 *   pthread_cond_{init,destroy,wait,broadcast,signal}
 *   pthread_{create,join}
 *
 * This header is only on the include path for the MSVC kit (see
 * HackTvStudio.pro). MinGW uses its own winpthreads. */

#ifndef HACKTVSTUDIO_PTHREAD_MSVC_H
#define HACKTVSTUDIO_PTHREAD_MSVC_H

#ifndef _MSC_VER
#error "This pthread shim is for MSVC only; MinGW ships winpthreads."
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <errno.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- types -------------------------------------------------------------- */

typedef SRWLOCK pthread_mutex_t;
typedef CONDITION_VARIABLE pthread_cond_t;
typedef void pthread_mutexattr_t;
typedef void pthread_condattr_t;
typedef void pthread_attr_t;

typedef struct {
	HANDLE handle;
	void *(*start_routine)(void *);
	void *arg;
	void *retval;
} pthread_shim_thread_t;

typedef pthread_shim_thread_t *pthread_t;

/* ---- mutex -------------------------------------------------------------- */

static __inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *attr)
{
	(void)attr;
	InitializeSRWLock(m);
	return 0;
}

static __inline int pthread_mutex_destroy(pthread_mutex_t *m)
{
	(void)m; /* SRWLOCK needs no cleanup */
	return 0;
}

static __inline int pthread_mutex_lock(pthread_mutex_t *m)
{
	AcquireSRWLockExclusive(m);
	return 0;
}

static __inline int pthread_mutex_unlock(pthread_mutex_t *m)
{
	ReleaseSRWLockExclusive(m);
	return 0;
}

/* ---- condition variable ------------------------------------------------- */

static __inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *attr)
{
	(void)attr;
	InitializeConditionVariable(c);
	return 0;
}

static __inline int pthread_cond_destroy(pthread_cond_t *c)
{
	(void)c; /* CONDITION_VARIABLE needs no cleanup */
	return 0;
}

static __inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
	return SleepConditionVariableSRW(c, m, INFINITE, 0) ? 0 : EINVAL;
}

static __inline int pthread_cond_broadcast(pthread_cond_t *c)
{
	WakeAllConditionVariable(c);
	return 0;
}

static __inline int pthread_cond_signal(pthread_cond_t *c)
{
	WakeConditionVariable(c);
	return 0;
}

/* ---- threads ------------------------------------------------------------ */

static DWORD WINAPI pthread_shim_trampoline(LPVOID p)
{
	pthread_shim_thread_t *t = (pthread_shim_thread_t *)p;
	t->retval = t->start_routine(t->arg);
	return 0;
}

static __inline int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                   void *(*start_routine)(void *), void *arg)
{
	pthread_shim_thread_t *t;
	(void)attr;

	t = (pthread_shim_thread_t *)calloc(1, sizeof(*t));
	if (!t)
		return EAGAIN;

	t->start_routine = start_routine;
	t->arg = arg;
	t->handle = CreateThread(NULL, 0, pthread_shim_trampoline, t, 0, NULL);
	if (!t->handle) {
		free(t);
		return EAGAIN;
	}

	*thread = t;
	return 0;
}

static __inline int pthread_join(pthread_t thread, void **retval)
{
	if (!thread)
		return EINVAL;

	WaitForSingleObject(thread->handle, INFINITE);
	CloseHandle(thread->handle);
	if (retval)
		*retval = thread->retval;
	free(thread);
	return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* HACKTVSTUDIO_PTHREAD_MSVC_H */
