#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>

#include <cassert>

#include "hist.h"

namespace lktrace {

// parsed log entry struct
struct log_entry {
	event ev; // event code
	size_t ts; // timestamp
	size_t addr; // address of sync object (per-thrd log) or tid (per-obj log)
	std::string caller; // string name of caller
	size_t offset; // offset from caller addr
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

	// locking pattern results per-thread
	// key=tid
	std::unordered_map<size_t, std::vector<std::pair<std::string, unsigned int> > >
		lk_patterns;

	// tid of master
	//const size_t master_tid;

	public:
	parser(std::string);

	void dump_threads(std::ostream&);
	void dump_patterns(std::ostream&);

	void find_patterns();
};

} // namespace lktrace
