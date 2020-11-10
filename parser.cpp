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
			// delimiting newling
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
		std::unordered_map<std::string, unsigned int> pat_map;

		std::string pat;
		int lk_count = 0;
		for (log_entry& e : hist) {
			switch (e.ev) {
			case (event::LOCK_ACQ):
				++lk_count;
				pat += (char) e.ev;
				break;
			case (event::LOCK_REL):
				--lk_count;
				pat += (char) e.ev;
				
				if (lk_count == 0) { // quiescent point
					pat_map[pat]++;
					pat.clear();
				}
				break;
			default:
				break;
			}
		}
		// write results to a more convenient format
		std::vector<std::pair<std::string, unsigned int> >
			pat_v (pat_map.begin(), pat_map.end());
		pat_map.clear();
		lk_patterns.insert(std::make_pair(tid, std::move(pat_v))); // clears pat_v
	}
}

void parser::dump_patterns (std::ostream& outs) {
	for (auto pat_v : lk_patterns) {
		size_t tid = pat_v.first;
		auto pats = pat_v.second;

		outs << "Thread 0x" << std::hex << tid << " (hook=" << thrd_hooks.at(tid)
		       << "):\n";	

		for (auto pat: pats) {
			outs << "Pattern ";
			for (auto c : pat.first) outs << ev_to_descr((event) c) << ">";
			outs << " occurs " << std::dec << pat.second << " time(s).\n";
		}
		outs << '\n';
	}
}

void parser::dump_threads(std::ostream& outs) {
	for (auto it = thrd_hist.begin(); it != thrd_hist.end(); ++it) {
		size_t tid = it->first;
		auto hist = it->second;
		std::string& hook = thrd_hooks.at(tid);

		outs << "Thread 0x" << std::hex << tid << " (hook=" << hook << "):\n";
		for (log_entry& L : hist) {
			outs << ev_to_descr(L.ev) << " 0x" << L.addr
				<< " in " << L.caller << '+' << L.offset << '\n';
		}
		outs << '\n';
	}
}

} // namespace lktrace
