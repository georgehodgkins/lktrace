#include "parser.h"

#define CHECKED_CONSUME(stream, c) \
	if (stream.peek() != c) { \
		std::cerr << "actual: "<< stream.get() << ' ' \
			<< std::hex << stream.rdstate() << '\n'; \
		assert(stream.peek() == c); \
	} \
	stream.get() // semicolon absence intentional

#define NEW_LOG(key) std::make_pair(key, std::vector<log_entry>())
#define NEW_REFLOG(key) std::make_pair(key, std::vector<log_entry_ref>())

namespace lktrace {

parser::parser(std::string fname) : 
	thrd_hist(), lk_hist(), thrd_hooks() {
	
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

	// build global and per-object histories
	// basically, merge sort the per-thread histories by timestamp, ascending
	// pair of index into hist vector, and tid of hist vector
	// TODO: this is just log_entry_ref backwards
	// TODO: actually build per-object histories
	std::vector<std::pair<size_t, size_t> > merge;
	
	for (auto it = thrd_hist.begin(); it != thrd_hist.end(); ++it)
		merge.push_back(std::make_pair(0, it->first));

	while (1) {
		// find the min timestamp out of current top entries
		log_entry L = {event::NULL_EVENT, std::numeric_limits<size_t>::max(), 0, 0};
		size_t tid = 0;
		size_t *ind = nullptr;
		for (auto& m : merge) {
			if (m.first < thrd_hist.at(m.second).size()
					&& thrd_hist.at(m.second).at(m.first).ts < L.ts) {

				ind = &m.first;
				tid = m.second;
			       	L = thrd_hist.at(m.second).at(m.first);	
			}
		}
		if (tid == 0) break; // all histories have been consumed

		// add to global hist
		log_entry_ref R = {tid, *ind};
		global_hist.push_back(R);
		++(*ind); // increment index
	}

#ifndef NDEBUG // validate global_hist
	for (auto& e: global_hist) {
		static size_t prev_ts = 0;
		assert(get_ref(e).ts >= prev_ts && "Global hist not ordered!");
		prev_ts = get_ref(e).ts;
	}
	size_t global_ev_count = 0;
	for (auto& h: thrd_hist) {
		global_ev_count += h.second.size();
	}
	assert(global_ev_count == global_hist.size() && "Events missing from global hist!");
#endif

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

void parser::dump_patterns_txt (std::ostream& outs, size_t min_depth) {
	for (auto pat_v : lk_patterns) {
		size_t tid = pat_v.first;
		auto pat_map = pat_v.second;

		outs << "=====\n";
		outs << "Thread 0x" << std::hex << tid << " (hook=" << thrd_hooks.at(tid)
		       << "):\n";	

		for (auto pat: pat_map) {
			const std::u16string& sig = pat.first;
			if (sig.size()/2 >= min_depth) {
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

void parser::dump_patterns(std::ostream& outs) {

for (auto& P : patterns) {
	assert(P.first.size() % 2 == 0);
	unsigned pat_len = P.first.size()/2;
	unsigned short depth = 0;
	bool waiting = false;

	for (unsigned i = 0; i < pat_len; ++i) {
		event ev = (event) P.first[i];
		size_t caller = id_caller[(size_t) P.first[i+pat_len]];
		std::stringstream msg;

		switch (ev) {
		case (event::LOCK_ACQ):
			assert(!waiting);
			++depth;
			msg << "Lock 0x" << std::hex << caller_xref[caller] << ": " <<
				caller_names[caller] << " [0x" << caller << ']';
			break;
		case (event::LOCK_REL):
			assert(depth > 0);
			assert(!waiting);
			--depth;
			msg << "Unlock 0x" << std::hex << caller_xref[caller] << ": " <<
				caller_names[caller] << " [0x" << caller << ']';
			break;
		case (event::COND_WAIT):
			assert(depth > 0);
			assert(!waiting);
			waiting = true;
			msg << "Cond Wait 0x" << std::hex << caller_xref[caller] << ": "
				<< caller_names[caller] << " [0x" << caller << ']';
			break;
		case (event::COND_LEAVE):
			assert(depth > 0);
			assert(waiting);
			waiting = false;
			msg << "Cond Wake 0x" << std::hex << caller_xref[caller] << ": "
				<< caller_names[caller] << " [0x" << caller << ']';
			break;
		case (event::COND_SIGNAL):
			assert(depth > 0);
			assert(!waiting);
			msg << "Cond Sig 0x" << std::hex << caller_xref[caller] << ": " <<
				caller_names[caller] << " [0x" << caller << ']';
			break;
		case (event::COND_BRDCST):
			assert(depth > 0);
			assert(!waiting);
			msg << "Cond Brd 0x" << std::hex << caller_xref[caller] << ": " <<
				caller_names[caller] << " [0x" << caller << ']';
			break;
		default:
			assert(false && "Incorrect event in pattern!");
		}

		for (unsigned short x = 0; x < depth; ++x)
			outs << ((waiting) ? '.' : '|');

		if (depth == 0) outs << '|';

		outs << msg.str() << '\n';		

		for (unsigned short x = 0; x < depth; ++x)
			outs << ((waiting) ? '.' : '|');
		
		if (depth > 0)
			outs << '\n';
	}
	
	for (auto& I : P.second.instances) {
		outs << std::dec << I.second << " occurrences in thread 0x" << std::hex <<
			I.first << " [" << thrd_hooks[I.first] << "]\n";
	}
	outs << "Mean time in pattern: " <<
		(double) P.second.total_time / (double) P.second.instances.size()
		<< " ticks\n\n";
}

}

void parser::dump_global(std::ostream& outs) {
	for (auto it = global_hist.begin(); it != global_hist.end(); ++it) {
		log_entry& L = thrd_hist[it->tid][it->ind];
		outs << std::hex << "0x" << it->tid << '\t' << ev_code_to_str(L.ev)
			<< "\t0x" << L.obj << '\t' << caller_names[L.caller] << '\t'
			<< std::dec << L.ts << '\n';
	}
}

inline char16_t parser::get_caller_id (size_t caller) {
	auto c_it = caller_id.find(caller);
	if (c_it == caller_id.end()) {
		assert(id_caller.size() < (1 << 16));
		auto emplit = caller_id.emplace(std::make_pair(caller,
					(char16_t) id_caller.size()));
		assert(emplit.second);
		c_it = emplit.first;
		id_caller.push_back(caller);
	}
	return c_it->second;
}

void parser::find_deps (size_t min_depth) {
	size_t holder_tid = 0;
	size_t init_time = 0;
	bool skip_wait_unlock = false;
	unsigned depth = 0;
	auto next = global_hist.end();
	std::u16string pattern;
	std::u16string callers;
	// records timestamp of next lock release for a tid
	// used to avoid recording inner patterns multiple times
	std::unordered_map<size_t, size_t> next_release;

	// walk global hist, uninterleaving and finding per-thread patterns
	for (auto Rit = global_hist.begin(); Rit != global_hist.end(); ++Rit ) {

	const log_entry_ref& R = *Rit;
	const log_entry& L = get_ref(R);

	switch (L.ev) {
	
	case (event::LOCK_ACQ):
		if (depth == 0) { // not in pattern, start new one
			assert(!skip_wait_unlock);
			holder_tid = R.tid;
			init_time = L.ts;
			++depth;
			pattern += (char16_t) L.ev;
			callers += get_caller_id(L.caller);
		} else if (R.tid == holder_tid) { // relevant event
			if (!skip_wait_unlock) {
				++depth;
				pattern += (char16_t) L.ev;
				callers += get_caller_id(L.caller);
			} else {
				assert(pattern.back() == (char16_t) event::COND_LEAVE);
				skip_wait_unlock = false;
			}
		} else if (next == global_hist.end()) { 
			// record the next beginning of an interleaved pattern
			// if it is the outermost in its thread, and one is not already recorded
			auto f_it = next_release.find(R.tid);
			if (f_it != next_release.end() && L.ts > f_it->second)
				next = Rit;
		}
		break;
	case (event::LOCK_REL):
		if (R.tid == holder_tid) {


		if (!skip_wait_unlock) {

		pattern += (char16_t) L.ev;
		callers += get_caller_id(L.caller);

		if (depth == 1) { // end of pattern, commit and reset
			assert(pattern.size() == callers.size());
			if (pattern.size()/2 >= min_depth) {
				pattern += callers;
				pattern_data& pdat = patterns[pattern];
				pdat.instance(holder_tid);
				pdat.total_time += (L.ts - init_time);
			}
			// do not record any more patterns for this tid
			// with a timestamp lower than this one
			next_release[holder_tid] = L.ts;

			// reset info
			pattern.clear();
			callers.clear();
			init_time = 0;
			holder_tid = 0;

			// go back to the beginning of the next interleaved CS
			// if one was noted
			if (next != global_hist.end()) {
				Rit = next;
				--Rit;
				next = global_hist.end();
			}

		}

		--depth;
		} else { // skip_wait_unlock = true
			assert(pattern[pattern.size()-1] == (char16_t) event::COND_WAIT);
		}
		} // R.tid == holder_tid
		
		break;
	case (event::COND_WAIT):
		if (R.tid == holder_tid) {
			skip_wait_unlock = true;
			pattern += (char16_t) L.ev;
			callers += get_caller_id(L.caller);
		}
		break;
	case (event::COND_LEAVE):
		if (R.tid == holder_tid) {
			pattern += (char16_t) L.ev;
			callers += get_caller_id(L.caller);
		}
		break;
	case (event::COND_SIGNAL):
	case (event::COND_BRDCST):
		if (R.tid == holder_tid) {
			pattern += (char16_t) L.ev;
			callers += get_caller_id(L.caller);
		}
		break;
	default:
		break;
	}
	} // for

}

} // namespace lktrace

#include "parser_viz.cpp"

