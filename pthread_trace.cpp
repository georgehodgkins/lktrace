#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h> 
#include <dlfcn.h>
#include <execinfo.h>
#include <iostream>
#include "tracer.h"

#define NEW_GLIBC_VERSTR "GLIBC_2.3.2" 
#define OLD_GLIBC_VERSTR "GLIBC_2.2.5"
// look up actual pthreads function
// TODO: replace dlsym() with hacking pthreads fn table
#define GET_REAL_FN(name, verstr, rtn, params...) \
	typedef rtn (*real_fn_t)(params); \
	static const real_fn_t REAL_FN = (real_fn_t) dlvsym(RTLD_NEXT, #name, verstr); \
	assert(REAL_FN != NULL) // semicolon absence intentional


//#define LKTRACE_DEBUG 1
lktrace::tracer the_tracer;

// trying to trace locks inside the allocator causes infinite recursion leading to deadlock
// this function detects that, with the help of lookups in the tracer ctor
// only checks the caller two frames up
//
// this has to be checked up here rather than in tracer becuase dlsym allocates stuff
bool caller_is_allocator() {
	// never trace if the tracer ctor is not complete yet
	if (the_tracer.alloc_start == 0) return true;
	
	void* buf[3];
	backtrace(buf, 3);
	return ((size_t) buf[2] > the_tracer.alloc_start &&
			(size_t) buf[2] < the_tracer.alloc_end);
}

// the __ methods are the actual function that the normal ones alias
// handy for us, since a dlsym call inside the memory allocator causes infinite recursion
//
// for some reason, libpthread.so only exposes the leading underscore variants for mutex
// functions, even though the naming convention is the same for condvar fns
//
// hopefully there's not a memory allocator out there that uses condvars
extern "C" int __pthread_mutex_lock(pthread_mutex_t*);
int pthread_mutex_lock(pthread_mutex_t* lk) {
	
	// log arrival at lock
	if (!caller_is_allocator())
		the_tracer.add_event(lktrace::event::LOCK_REQ, (size_t) lk);
	// run pthreads function
//	GET_REAL_FN(pthread_mutex_lock, int, pthread_mutex_t*);
//	int e = REAL_FN(lk);
	int e = __pthread_mutex_lock(lk);
	if (!caller_is_allocator()) {
		if (e == 0) the_tracer.add_event(lktrace::event::LOCK_ACQ, (size_t) lk);
		else the_tracer.add_event(lktrace::event::LOCK_ERR, (size_t) lk);
	}
	return e;
}

extern "C" int __pthread_mutex_unlock(pthread_mutex_t*);
int pthread_mutex_unlock(pthread_mutex_t* lk) {
	// log lock release
	if(!caller_is_allocator())
		the_tracer.add_event(lktrace::event::LOCK_REL, (size_t) lk);
	// run pthreads function
//	GET_REAL_FN(pthread_mutex_unlock, int, pthread_mutex_t*);
//	return REAL_FN(lk);
	return __pthread_mutex_unlock(lk);
}

int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* lk) {
	// log arrival at wait
	if (!caller_is_allocator())
		the_tracer.add_event(lktrace::event::COND_WAIT, (size_t) cond); 
	// run pthreads function
	GET_REAL_FN(pthread_cond_wait, NEW_GLIBC_VERSTR, 
			int, pthread_cond_t*, pthread_mutex_t*);
	int e = REAL_FN(cond, lk);
	if (!caller_is_allocator()) {
		if (e == 0) the_tracer.add_event(lktrace::event::COND_LEAVE, (size_t) cond);
		else the_tracer.add_event(lktrace::event::COND_ERR, (size_t) cond);
	}
	return e;
}

int pthread_cond_signal(pthread_cond_t* cond) {
	// log cond signal
	if (!caller_is_allocator())
		the_tracer.add_event(lktrace::event::COND_SIGNAL, (size_t) cond);
	// run pthreads function
	GET_REAL_FN(pthread_cond_signal, NEW_GLIBC_VERSTR, int, pthread_cond_t*);
	return REAL_FN(cond);
}

int pthread_cond_broadcast(pthread_cond_t* cond) {
	if (!caller_is_allocator())
		the_tracer.add_event(lktrace::event::COND_BRDCST, (size_t) cond);
	GET_REAL_FN(pthread_cond_broadcast, NEW_GLIBC_VERSTR, int, pthread_cond_t*);
	return REAL_FN(cond);
}

// wraps a thread hook (entry point and args) so that we can pass it through
// the injected tracking code
struct pthr_hook {
	void* (*hook)(void*);
	void* arg;
	void* caller;

	pthr_hook(pthr_hook*);
	pthr_hook(void* (*)(void*), void*, void*);
};
pthr_hook::pthr_hook(pthr_hook* dyn):
	hook(dyn->hook), arg(dyn->arg), caller(dyn->caller) {
}

pthr_hook::pthr_hook(void* (*h)(void*), void* a, void*c):
	hook(h), arg(a), caller(c) {};

void* inject_thread_registration (void* real) {
	// get a local copy of real thread hook/args
	// then delete the dynalloc'd copy
	pthr_hook* real_dyn = (pthr_hook*) real;
	pthr_hook real_thread (real_dyn);
	delete real_dyn;
	// register thread
	the_tracer.add_this_thread((size_t) real_thread.hook, real_thread.caller);
	// use unified return point
	pthread_exit(real_thread.hook(real_thread.arg));
}

int pthread_create (pthread_t* thread, const pthread_attr_t *attr, 
		void *(*hook)(void*), void* arg) {
	GET_REAL_FN(pthread_create, OLD_GLIBC_VERSTR, int, pthread_t*,
			const pthread_attr_t*, void* (*) (void*), void*);
	// we inject some tracking code before starting the real thread
	//  and record our calling function
	void* buf[2];
	backtrace(buf, 2);
	pthr_hook* real_thread = new pthr_hook(hook, arg, buf[1]);
	return REAL_FN(thread, attr, inject_thread_registration,
			(void*) real_thread);
}

void pthread_exit (void* rtn) {
	the_tracer.sever_this_thread();
	GET_REAL_FN(pthread_exit, OLD_GLIBC_VERSTR, void, void*);
	while (1) REAL_FN(rtn); // loop is there to convince compiler that this does not return
}


