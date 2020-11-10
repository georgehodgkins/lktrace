#pragma once

#define MAX_THRD_COUNT 100 // defines max size of hash map used

#include <vector> // event histories 
#include <chrono> // timestamps
#include <string> 
#include <fstream>

#include <cstdlib> // free()
#include <cassert> 
#include <cstring> // strchr() for parsing function names

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h> // dladdr(), dlsym() linker symbol lookup (for stack tracing)
#include <link.h> // dl_iterate_phdr() linker symbol lookup
#include <execinfo.h> // backtrace()
#include <unistd.h>  // free()
#include <cxxabi.h> // __cxx_demangle()

// this is what happens when you overuse templates
//
// also for some reason I had to include an internal
// header directly for the code to compile
// TODO: replace cds
#include <cds/details/make_const_type.h>
#include <cds/init.h>
#include <cds/gc/nogc.h>
#include <cds/container/michael_map_nogc.h>
#include <cds/container/michael_kvlist_nogc.h>

#include "hist.h"
//#include <pthread.h>


namespace lktrace {

using namespace std;

			
// history type (concurrent hash map from tids to vectors of hist_entry)
// does not support removing entries bcs garbage collection is turned off
using hist_map = cds::container::MichaelHashMap<cds::gc::nogc,
      cds::container::MichaelKVList<cds::gc::nogc, size_t, vector<hist_entry> > >;

class tracer {
	
	// per-thread event histories
	hist_map histories;

	// at least one thread other than the master has been created
	bool multithreaded;
	
	// time zero
	const chrono::time_point<chrono::steady_clock> init_time;

	// register thread with libcds tracking
	static void cds_register_thread();

	public:
	tracer();
	~tracer();

	// add a new thread (new hash map entry) with tid of current
	void add_this_thread(size_t hook, void* caller, bool mt = true);
	// add THRD_EXIT event and remove thread from cds tracking
	// obviously, does not delete the thread history
	void sever_this_thread(bool mt = true);

	// add a new event
	void add_event(event, size_t);
	// alias for pthread_self, pretty much
	static size_t get_tid();	

}; 

} // namespace lktrace 
