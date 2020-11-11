#include "parser.h"

#define CHECKED_CONSUME(stream, c) \
	assert(stream.peek() == c); \
	stream.get() // semicolon absence intentional

#define NEW_LOG(key) std::make_pair(key, std::vector<log_entry>())

namespace lktrace {

parser::parser(std::string fname) : 
	thrd_hist(), lk_hist(), cond_hist(), thrd_hooks() {
	
	std::ifstream trace (fname);
	assert(trace.is_open());

	while(!trace.eof()) {
		char tdes = trace.get();
		if (tdes < 0) break; // TODO: find out why EOF isn't working
		CHECKED_CONSUME(trace, ':');
		size_t tid = 0;
		trace >> std::hex >> tid;
		assert (tid != 0);
		CHECKED_CONSUME(trace, ':');
		std::string hook_name;
		getline(trace, hook_name, ':');
		assert(!hook_name.empty());
		thrd_hooks.emplace(std::make_pair(tid, hook_name));
		CHECKED_CONSUME(trace, '\n');

		// add new thread history
		auto emplit = thrd_hist.emplace(NEW_LOG(tid));
		assert(emplit.second == true);
		auto thrd_l_it = emplit.first;
		
		while (trace.peek() != '\n') { // thread hists delimited by double newline
			log_entry L; // per-thread log entry
			// get timestamp
			trace >> std::dec >> L.ts;
			assert(L.ts != 0);
			CHECKED_CONSUME(trace, ':');
			// get event
			std::string es;
			getline(trace, es, ':'); // consumes colon
			assert(es.size() == 2);
			L.ev = ev_str_to_code(es);
			// get obj addr
			trace >> std::hex >> L.addr;
			//assert(L.addr != 0);
			CHECKED_CONSUME(trace, ':');
			// get caller string
			getline(trace, L.caller, '+'); // consumes plus
			assert(!L.caller.empty());
			// get offset
			trace >> L.offset;
			assert(L.offset != 0);
			CHECKED_CONSUME(trace, ':');
			// get full caller addr
			trace >> L.call_addr;
			assert(L.call_addr != 0);

			// add caller name to reference map
			std::stringstream name;
			name << L.caller << '+' << std::hex << L.offset;
			caller_names.insert(
				std::make_pair(L.call_addr, name.str()));
			name.str("");

			// add per-obj log entry
			log_entry L2 = L;
			size_t obj_addr = L2.addr;
			L2.addr = tid;
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

			// add per-thread log entry
			thrd_l_it->second.push_back(std::move(L));
			// delimiting newline
			CHECKED_CONSUME(trace, '\n');
		}

		CHECKED_CONSUME(trace, '\n');
	}
}

// find all unique critical section patterns
void parser::find_patterns () {
	for (auto h : thrd_hist) {
		size_t tid = h.first;
		std::vector<log_entry>& hist = h.second;
		// we represent patterns as strings of chars cast from event codes
		// and count numbers of occurrences with this hash map
		std::unordered_multimap<std::u16string,
			std::pair<std::vector<size_t>, size_t> > pat_map;
		std::vector<size_t> caller_list;

		std::u16string pat; // pattern of events
		std::u16string id_pat; // pattern of lock ids
		int lk_count = 0; // number of currently held locks
		char16_t nxt_id = 0; // identifiers for each new lock encountered
		std::unordered_map<size_t, char16_t> lk_id; // lock identifiers
		for (log_entry& e : hist) {
			if (e.ev == event::LOCK_ACQ || e.ev == event::LOCK_REL) {
				// identify the lock 
				auto it = lk_id.find(e.addr);
				if (it == lk_id.end()) { // new lock encountered
					lk_id.insert(std::make_pair(e.addr, nxt_id));
					id_pat += nxt_id;
					++nxt_id;
				} else {
					id_pat += it->second;
				}

				// add this caller to caller list
				caller_list.push_back(e.call_addr);

				switch (e.ev) {
				case (event::LOCK_ACQ):
					++lk_count;
					pat += (char16_t) e.ev;

					break;
				case (event::LOCK_REL):
					--lk_count;
					pat += (char16_t) e.ev;	
					if (lk_count == 0) { // quiescent point
						pat += id_pat;
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
						id_pat.clear();
						pat.clear();
						nxt_id = 0;
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
			for (size_t a = 0, b = sig.find_first_of((char16_t) 0);
					b < sig.size(); ++a, ++b) {

				outs << ev_to_descr((event) sig[a]) << " "
					<< (unsigned int) sig[b] << " @" <<
					caller_names[caller_addrs[a]];
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

		outs << "=====\n";
		outs << "Thread 0x" << std::hex << tid << " (hook=" << hook << "):\n";
		for (log_entry& L : hist) {
			outs << ev_to_descr(L.ev) << " 0x" << L.addr
				<< " in " << L.caller << '+' << L.offset 
				<< " [0x" << L.call_addr << "]\n";
		}
		outs << '\n';
	}
}

} // namespace lktrace
