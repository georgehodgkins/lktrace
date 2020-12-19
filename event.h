#pragma once
#include <chrono>
#include <execinfo.h>
#include <cassert>
#include <string>
#include <cctype>

namespace lktrace {

// these are 16-bit and count down from the top so that
// the weird pattern matching method we use works properly
// see find_patterns() in parser.cpp
// TODO: make this a real class instead
enum class event : uint16_t {
	LOCK_REQ = 0xFFFF, LOCK_ACQ = 0xFFFE, LOCK_REL = 0xFFFD,
	LOCK_ERR = 0xFFFC, LOCK_EVENT_TYPE = 0xF000,
	COND_WAIT = 0xEFFF, COND_LEAVE = 0xEFFE, COND_SIGNAL = 0xEFFD,
	COND_BRDCST = 0xEFFC, COND_ERR = 0xEFFB, COND_EVENT_TYPE = 0xE000,
       	THRD_SPAWN = 0xDFF6, THRD_EXIT = 0xDFF5, THRD_EVENT_TYPE = 0xD000,
	NULL_EVENT = 0x0};

inline event ev_str_to_code(std::string str) {
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

inline std::string ev_to_descr(event ev, bool lower = false) {
	assert(ev != event::NULL_EVENT);
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

	if (lower) str[0] = std::tolower(str[0]);	

	return str;
}

inline std::string ev_code_to_str (event ev) {
	assert(ev != event::NULL_EVENT);
	switch (ev) {
		case (event::LOCK_REQ):
			return "LQ";
		case (event::LOCK_ACQ):
			return "LA";
		case (event::LOCK_REL):
			return "LR";
		case (event::LOCK_ERR):
			return "LE";
		case (event::COND_WAIT):
			return "CW";
		case (event::COND_LEAVE):
			return "CL";
		case (event::COND_SIGNAL):
			return "CS";
		case (event::COND_BRDCST):
			return "CB";
		case (event::COND_ERR):
			return "CE";
		case (event::THRD_SPAWN):
			return "TS";
		case (event::THRD_EXIT):
			return "TE";
		default:
			assert(false && "Unrecognized event!");
	}
}

} // namespace lktrace
