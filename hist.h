#include <chrono>
#include <execinfo.h>
#include <cassert>
#include <string>

namespace lktrace {

enum class event : char {LOCK_REQ, LOCK_ACQ, LOCK_REL, LOCK_ERR, 
	COND_WAIT, COND_LEAVE, COND_SIGNAL, COND_BRDCST, COND_ERR,
	THRD_SPAWN, THRD_EXIT};

// thread history entry
struct hist_entry {
	std::chrono::time_point<std::chrono::steady_clock> ts;
	event ev;
	void* caller;
	size_t addr;

	// ctor that looks up caller	
	hist_entry(event, size_t);
	// pass in caller to ctor
	hist_entry(event, size_t, void*);

	// start and end address of our own code (for stack tracing) 
	static size_t start_addr;
	static size_t end_addr; // technically the start addr of the next .so
};

std::string ev_to_descr (event);
event ev_str_to_code (std::string);



} // namespace lktrace
