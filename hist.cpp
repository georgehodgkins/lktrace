#include "hist.h"

// how many levels up in the stack we look for code 
#define TRACE_DEPTH 8

namespace lktrace {

using namespace std;

// define static vars
size_t hist_entry::start_addr = 0;
size_t hist_entry::end_addr = 0;

hist_entry::hist_entry(event e, size_t obj_addr) : 
	ts(chrono::steady_clock::now()), ev(e), addr(obj_addr) {

	// these should be set in the tracer ctor	
	assert(start_addr && end_addr);
		
	void* buf[TRACE_DEPTH];
	int v = backtrace(buf, TRACE_DEPTH);
	int a = 0;
	// find first frame outside of our own code
	while (a < v && start_addr < (size_t) buf[a] &&
		end_addr > (size_t) buf[a]) ++a;
	assert(a < v);
	caller = buf[a]; 
}

// ctor overload to manually set caller addr rather than looking it up
hist_entry::hist_entry(event e, size_t obj_addr, void* c) :
	ts(chrono::steady_clock::now()), ev(e), caller(c), addr(obj_addr) {}

event ev_str_to_code(std::string str) {
	event ev;
	switch (str[0]) {
	case ('T'):
		switch(str[1]) {
			case ('S'): ev = event::THRD_SPAWN; break;
			case ('E'): ev = event::THRD_EXIT; break;
			default: assert(false); break;
		}
		break;
	case ('L'):
		switch(str[1]) {
			case ('R'): ev = event::LOCK_REL; break;
			case ('A'): ev = event::LOCK_ACQ; break;
			case ('Q'): ev = event::LOCK_REQ; break;
			case ('E'): ev = event::LOCK_ERR; break;
			default: assert(false); break;
		}
		break;
	case ('C'):
		switch(str[1]) {
			case ('W'): ev = event::COND_WAIT; break;
			case ('L'): ev = event::COND_LEAVE; break;
			case ('S'): ev = event::COND_SIGNAL; break;
			case ('B'): ev = event::COND_BRDCST; break;
			case ('E'): ev = event::COND_ERR; break;
			default: assert(false); break;
		}
		break;
	default:
		assert(false);
		break;
	}
	return ev;
}

std::string ev_to_descr(event ev) {
	std::string str;
	switch (ev) {
	case (event::THRD_SPAWN): str = "Spawned thread"; break;
	case (event::THRD_EXIT): str = "Exited thread"; break;
	case (event::LOCK_REQ): str = "Blocked on lock"; break;
	case (event::LOCK_ACQ): str = "Acquired lock"; break;
	case (event::LOCK_REL): str = "Released lock"; break;
	case (event::LOCK_ERR): str = "Error acquiring lock"; break;
	case (event::COND_WAIT): str = "Blocked on condvar"; break;
	case (event::COND_LEAVE): str = "Woke from condvar"; break;
	case (event::COND_SIGNAL): str = "Signaled condvar"; break;
	case (event::COND_BRDCST): str = "Broadcasted condvar"; break;
	case (event::COND_ERR): str = "Error waiting on condvar"; break;
	}
	return str;
}
} // namespace lktrace
