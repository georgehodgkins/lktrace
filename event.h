#pragma once
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

inline std::string ev_to_descr(event ev) {
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

inline std::string ev_code_to_str (event ev) {
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
			return "<bad>";
	}
}

} // namespace lktrace
