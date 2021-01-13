#include <fstream>
#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <iostream>
#include <limits>
#include <functional>

#include <cassert>
#include <cmath>

#include "event.h"
#include "enum_ops.h"

namespace lktrace {

// parsed log entry struct
struct log_entry {
	event ev; // event code
	size_t ts; // timestamp
	size_t obj; // address of sync object
	size_t caller; // address of caller

	bool operator== (const log_entry& p) const {
		return (ev == p.ev && ts == p.ts && obj == p.obj && caller == p.caller);
	}
};

// reference to a log entry
// we use this rather than a more compact iterator or ptr because the
// context often needs the tid as well
struct log_entry_ref {
	size_t tid;
	size_t ind;

	bool operator== (const log_entry_ref& p) const {
		return tid == p.tid && ind == p.ind;
	}

	operator bool () const {
		return tid != 0;
	}

	static log_entry_ref null () {
		log_entry_ref N = {0, 0};
		return N;
	}
};

struct pattern_data {
	// tid of threads where the pattern occurs,
	// and count of occurrences
	std::vector<std::pair<size_t, size_t>> instances;

	void instance (size_t tid) {
		auto it = instances.begin();
		for (; it != instances.end(); ++it)
			if (it->first == tid) {
				++(it->second);
				break;
			}
		if (it == instances.end()) instances.push_back(std::make_pair(tid, 1));
	}
	
	size_t total_time;
	size_t wait_time;
};

namespace viz {
	struct thrd_dat;
}

class parser {

	// per-thread histories (key=tid)
	// this stores the actual event objects, other orderings point here
	std::unordered_map<size_t, std::vector<log_entry> > thrd_hist;

	// per-lock histories (key=lock addr)
	std::unordered_map<size_t, std::vector<log_entry_ref> > lk_hist;

	std::unordered_map<std::u16string, pattern_data> patterns;
	std::unordered_map<size_t, char16_t> caller_id;
	std::vector<size_t> id_caller;

	// globally ordered history
	std::vector<log_entry_ref> global_hist;

	friend class viz::thrd_dat;
	protected:
	// symbol names of thread hooks (or filenames if symbol name was not found)
	// key=tid
	std::unordered_map<size_t, std::string> thrd_hooks;

	// resolved names of caller addresses
	// key=in-memory addr
	std::unordered_map<size_t, std::string> caller_names;

	private:
	// corresponding object addrs for caller addresses
	std::unordered_map<size_t, size_t> caller_xref;
	
	// locking pattern results per-thread
	// key=tid -> key = pattern signature -> caller list + count
	std::unordered_map<size_t, std::unordered_multimap<
		std::u16string, std::pair<std::vector<size_t>, size_t> > > lk_patterns;

	const log_entry& get_ref (log_entry_ref R) const {
		return (thrd_hist.at(R.tid))[R.ind];
	}

	char16_t get_caller_id (size_t);

	public:
	parser(std::string);

	void dump_threads(std::ostream&);
	void dump_patterns(std::ostream&);
	void dump_patterns_txt(std::ostream&, size_t);
	void dump_global(std::ostream&);

	void find_patterns();
	void find_deps(size_t);
	int viz ();
};

} // namespace lktrace
