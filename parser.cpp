#include "parser.h"

#define CHECKED_CONSUME(stream, c) \
	if (stream.peek() != c) { \
		std::cerr << "actual: "<< stream.get() << ' ' \
			<< std::hex << stream.rdstate() << '\n'; \
		assert(stream.peek() == c); \
	} \
	stream.get() // semicolon absence intentional

#define NEW_LOG(key) std::make_pair(key, std::vector<log_entry>())

namespace lktrace {

parser::parser(std::string fname) : 
	thrd_hist(), lk_hist(), cond_hist(), thrd_hooks() {
	
	std::ifstream trace (fname);
	assert(trace.is_open());

	while(trace.peek() != -1) {
		// TODO: handle errors w/ exceptions
		CHECKED_CONSUME(trace, '[');
		std::string bdes; // block designator
		getline(trace, bdes, ':');
		if (bdes[0] == 't' || bdes[0] == 'm') { // a thread block
			size_t tid;
			trace >> std::hex >> tid;
			CHECKED_CONSUME(trace, ':');
			//if (bdes[1] == 'm') master_tid = tid;
			size_t hook;
			trace >> std::hex >> hook;
			CHECKED_CONSUME(trace, ']');
			CHECKED_CONSUME(trace, '\n');
			// add new thread history
			auto emplit = thrd_hist.emplace(NEW_LOG(tid));
			assert(emplit.second == true);
			auto thrd_l_it = emplit.first;
			// get event entries
			while (trace.peek() != '\n') {// double newline ends a sxn
				log_entry L;
				trace >> std::dec >> L.ts;
				CHECKED_CONSUME(trace, ':');
				std::string es;
				getline(trace, es, ':');
				L.ev = ev_str_to_code(es); 
				trace >> std::hex >> L.obj;
				CHECKED_CONSUME(trace, ':');
				trace >> L.caller;
				CHECKED_CONSUME(trace, '\n');

				// add per-thread log entry
				thrd_l_it->second.push_back(std::move(L));

				// add per-obj log entry
				log_entry L2 = L;
				size_t obj_addr = L2.obj;
				L2.obj = tid;
				if (es[0] == 'L') {
					auto it = lk_hist.find(obj_addr);
					if (it == lk_hist.end()) { // first use of this object
						auto emplit = lk_hist.emplace(NEW_LOG(obj_addr));
						it = emplit.first;
					}
					it->second.push_back(std::move(L2));
				} else if (es[0] == 'C') {
					auto it = cond_hist.find(obj_addr);
					if (it == cond_hist.end()) {
						auto emplit = cond_hist.emplace(NEW_LOG(obj_addr));
						it = emplit.first;
					}
					it->second.push_back(std::move(L2));
				}
				// add xref
				caller_xref.insert(std::make_pair(L.caller, L.obj));
			}
		} else if (bdes[0] == 'n') { // a string table block
			trace.ignore(8, '\n');
			while (trace.peek() != '\n') {
				std::pair<size_t, std::string> tab;
				trace >> std::hex >> tab.first;
				CHECKED_CONSUME(trace, ':');
				getline(trace, tab.second);
				caller_names.insert(std::move(tab));
			}
		}
		CHECKED_CONSUME(trace, '\n');
	}
	trace.close();

	// cross-reference thread hooks
	for (auto hist_v : thrd_hist) {
		auto it = caller_names.find(hist_v.second.front().obj);
		assert(it != caller_names.end());
		// map tid to hook
		// TODO: use hook addr instead
		thrd_hooks.insert(std::make_pair(hist_v.first, it->second));
	}
}
	
// find all unique critical section patterns
void parser::find_patterns () {
	for (auto h : thrd_hist) {
		size_t tid = h.first;
		std::vector<log_entry>& hist = h.second;
		// we represent patterns as strings of chars cast from event codes
		// and then differentiate between patterns based on lock object addrs
		std::unordered_multimap<std::u16string,
			std::pair<std::vector<size_t>, size_t> > pat_map;
		std::vector<size_t> caller_list;

		std::u16string pat; // pattern of events
		int lk_count = 0; // number of currently held locks
		for (log_entry& e : hist) {
			if (e.ev == event::LOCK_ACQ || e.ev == event::LOCK_REL) {

				// add this caller to caller list
				caller_list.push_back(e.caller);

				switch (e.ev) {
				case (event::LOCK_ACQ):
					++lk_count;
					pat += (char16_t) e.ev;

					break;
				case (event::LOCK_REL):
					--lk_count;
					pat += (char16_t) e.ev;	
					if (lk_count == 0) { // quiescent point
						// record pattern + caller set
						auto range = pat_map.equal_range(pat);
						while (range.first != range.second) {
						if (range.first->second.first == caller_list) {
							++(range.first->second.second);
							caller_list.clear();
							break;
						}
						++range.first;
						}

						if (!caller_list.empty())  // not found in map
							pat_map.insert(std::make_pair(
								pat, std::make_pair(
								std::move(caller_list), 1)));
						// reset pattern
						pat.clear();
					}
					break;
				default:
					break;
				}
			}
		}
		// record patterns for this thread
		lk_patterns.insert(std::make_pair(tid, std::move(pat_map))); // clears pat_map
	}
}

void parser::dump_patterns (std::ostream& outs) {
	for (auto pat_v : lk_patterns) {
		size_t tid = pat_v.first;
		auto pat_map = pat_v.second;

		outs << "=====\n";
		outs << "Thread 0x" << std::hex << tid << " (hook=" << thrd_hooks.at(tid)
		       << "):\n";	

		for (auto pat: pat_map) {
			const std::u16string& sig = pat.first;
			std::vector<size_t>& caller_addrs = pat.second.first;
			for (size_t a = 0; a < sig.size(); ++a) {

				outs << ev_to_descr((event) sig[a]) <<
					" [0x" << std::hex << caller_xref[caller_addrs[a]]
					<< "] @" << caller_names[caller_addrs[a]]; 
				//if (b != sig.size()-1) outs << " >";
				outs << '\n';
			}
			outs << " occurs " << std::dec << pat.second.second << " time(s).\n\n";
		}
		outs << '\n';
	}
}

void parser::dump_threads(std::ostream& outs) {
	for (auto it = thrd_hist.begin(); it != thrd_hist.end(); ++it) {
		size_t tid = it->first;
		auto hist = it->second;
		std::string& hook = thrd_hooks.at(tid);
		//std::string hook = "<optimized out>";

		outs << "=====\n";
		outs << "Thread 0x" << std::hex << tid << " (hook=" << hook << "):\n";
		for (log_entry& L : hist) {
			outs << ev_to_descr(L.ev) << " 0x" << L.obj
				<< " in " << caller_names[L.caller] 
				<< " [0x" << L.caller << "]\n";
		}
		outs << '\n';
	}
}

} // namespace lktrace
