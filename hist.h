#include <chrono>
#include <execinfo.h>
#include <cassert>
#include <string>

namespace lktrace {

// these are 16-bit and count down from the top so that
// the weird pattern matching method we use works properly
// see find_patterns() in parser.cpp
enum class event : char16_t {LOCK_REQ = 0xFFFF, LOCK_ACQ = 0xFFFE, LOCK_REL = 0xFFFD,
	LOCK_ERR = 0xFFFC, COND_WAIT = 0xFFFB, COND_LEAVE = 0xFFFA, COND_SIGNAL = 0xFFF9,
	COND_BRDCST = 0xFFF8, COND_ERR = 0xFFF7, THRD_SPAWN = 0xFFF6, THRD_EXIT = 0xFFF5};

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
