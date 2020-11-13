#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iostream>

#include <cassert>

#include "event.h"

namespace lktrace {

// parsed log entry struct
struct log_entry {
	event ev; // event code
	size_t ts; // timestamp
	size_t obj; // address of sync object (per-thrd log) or tid (per-obj log)
	size_t caller; // address of caller
};

class parser {

	// per-thread histories (key=tid)
	std::unordered_map<size_t, std::vector<log_entry> > thrd_hist;

	// per-lock histories (key=lock addr)
	std::unordered_map<size_t, std::vector<log_entry> > lk_hist;

	// per-condvar histories (key=cond addr)
	std::unordered_map<size_t, std::vector<log_entry> > cond_hist;

	// symbol names of thread hooks (or filenames if symbol name was not found)
	// key=tid
	std::unordered_map<size_t, std::string> thrd_hooks;

	// resolved names of caller addresses
	// key=in-memory addr
	std::unordered_map<size_t, std::string> caller_names;

	// corresponding object addrs for caller addresses
	std::unordered_map<size_t, size_t> caller_xref;
	
	// locking pattern results per-thread
	// key=tid -> key = pattern signature -> caller list + count
	std::unordered_map<size_t, std::unordered_multimap<
		std::u16string, std::pair<std::vector<size_t>, size_t> > > lk_patterns;

	// tid of master
//	const size_t master_tid;

	public:
	parser(std::string);

	void dump_threads(std::ostream&);
	void dump_patterns(std::ostream&);

	void find_patterns();
};

} // namespace lktrace
