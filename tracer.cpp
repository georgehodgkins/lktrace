#include "tracer.h"

extern void* _start;

namespace lktrace {

size_t tracer::get_tid () { // an alias for pthread_self, cast to size_t
	typedef pthread_t (*pthr_self_t)(void);
	static const pthr_self_t pthr_self = (pthr_self_t) dlsym(RTLD_NEXT, "pthread_self");
	assert(pthr_self != NULL);
	return (size_t) pthr_self();
}

// callback to find the .so after ours in the list (ie get our own address range)
// we pass in the start address in the data field, 
// the end address is returned in data
// TODO: use link map instead
int phdr_callback (dl_phdr_info *info, size_t sz, void* data) {
	static size_t prev = 0;
	if (info->dlpi_addr == *(size_t*) data) *(size_t*) data = prev;
	prev = (size_t) info->dlpi_addr;
	return 0;
}
	
tracer::tracer() : 
	histories(MAX_THRD_COUNT, 1), 
	init_time(chrono::steady_clock::now()) {
		// set up cds thread tracking
		cds::Initialize();
		// find beginning and end of our .so
		Dl_info info;
		dladdr((void*) &phdr_callback, &info);
		hist_entry::start_addr = (size_t) info.dli_fbase;
		size_t get_addr = hist_entry::start_addr;
		dl_iterate_phdr(&phdr_callback, (void*) &get_addr);
		assert(get_addr > hist_entry::start_addr);
		hist_entry::end_addr = get_addr;
		// register master thread
		void* buf[2];
		int e = backtrace(buf, 2);
		assert(e == 2);
		add_this_thread(0, buf[1],  false);
}

tracer::~tracer () { // purpose of this destructor is to write out our results
	// add thread exit event for master, but don't deregister w/cds
	sever_this_thread(false);

	if (multithreaded) { // don't write anything out if there was never >1 thread

	string fname = "lktracedat-";
	fname += to_string(getpid());

	ofstream outfile (fname);
	assert(outfile.is_open());

	// we write out each history individually
	for (auto hist_it = histories.begin(); hist_it != histories.end(); ++hist_it) {
		size_t tid = hist_it->first;
		vector<hist_entry> hist = hist_it->second;
		assert(hist.front().ev == event::THRD_SPAWN);
		// get the name of the thread hook
		Dl_info hook_info;
		if (hist.front().addr != 0) {
			int e = dladdr((void*) hist.front().addr, &hook_info);
			assert(e != 0);
		}
		if (tid == get_tid()) outfile << "M:";
		else outfile << "T:"; 
		outfile << hex << tid << ':';
		if (hist.front().addr == 0) {
			outfile << "<program entry point>";
		} else if (hook_info.dli_sname != NULL) {
			outfile << hook_info.dli_sname;
		} else {
			outfile << hook_info.dli_fname;
			// log offset here to help with debug
			outfile << '+' << hist.front().addr - (size_t) hook_info.dli_fbase; 
		}
		outfile << ":\n";

		for (hist_entry& entry : hist) {
			// write timestamp
			outfile << dec << (entry.ts - init_time).count() << ':';
			// write event code
			switch (entry.ev) {
				case (event::LOCK_REQ):
					outfile << "LQ";
					break;
				case (event::LOCK_ACQ):
					outfile << "LA";
					break;
				case (event::LOCK_REL):
					outfile << "LR";
					break;
				case (event::LOCK_ERR):
					outfile << "LE";
					break;
				case (event::COND_WAIT):
					outfile << "CW";
					break;
				case (event::COND_LEAVE):
					outfile << "CL";
					break;
				case (event::COND_SIGNAL):
					outfile << "CS";
					break;
				case (event::COND_BRDCST):
					outfile << "CB";
					break;
				case (event::COND_ERR):
					outfile << "CE";
					break;
				case (event::THRD_SPAWN):
					outfile << "TS";
					break;
				case (event::THRD_EXIT):
					outfile << "TE";
					break;
			}
			// write object addr
			outfile << ":0x" << hex << entry.addr << ':';
			// get and demangle caller name
			Dl_info caller_info;
			int e = dladdr(entry.caller, &caller_info);
			assert(e != 0);
			const char* name;
			long int offset;
			
			if (caller_info.dli_sname != NULL) { // found good match 
				name = caller_info.dli_sname;
				offset = (long int) entry.caller -
					(long int)caller_info.dli_saddr;
			} else { // no matching defn, just give filename & offset in file
				name = caller_info.dli_fname;
				offset = (long int) entry.caller - 
					(long int) caller_info.dli_fbase;
			}	
			// skip prepended filename
//			char* name_begin = strchr(mangled_name, '(') + 1;
//			if (name_begin != nullptr && 
//					name_begin[0] == '_' && name_begin[1] == 'Z') { 
//				// hopefully this string isn't used elsewhere because
//				// it's getting chopped up
//				char* name_end = strchr(name_begin, '+');
//				assert (name_end);
//				*name_end = '\0';
//
//				int stat = 0;
//				char* demangled_name = abi::__cxa_demangle(name_begin,
//						nullptr, nullptr, &stat);
//				assert(stat == 0);
//				outfile << demangled_name;
//				free(demangled_name);
//			} else { // name is not in expected format, just write it out
//				outfile << mangled_name;
//			}
			// write offset from function entry
			outfile << name << "+0x" << offset << '\n';

		}
		outfile << '\n';
	}

	outfile.close();
	} // if (multithreaded)


	// clean up cds
	cds::Terminate();
}

void tracer::add_this_thread(size_t hook, void* caller, bool mt) {
	multithreaded = mt;
	// register this thread with cds
	cds::threading::Manager::attachThread();
	// add new empty list to the history hashmap
	size_t tid = get_tid();
	histories.insert(tid);
	// get our list back
	hist_map::iterator hist = histories.contains(tid);
	assert(hist != histories.end());
	hist->second.emplace_back(event::THRD_SPAWN, hook, caller);
}

void tracer::sever_this_thread(bool mt) {
	// add thread exit event
	size_t tid = get_tid();
	hist_map::iterator hist = histories.contains(tid);
	assert(hist != histories.end());
	if (!mt) {
		void* buf[3];
		int e = backtrace(buf, 3);
		assert(e == 3);
		hist->second.emplace_back(event::THRD_EXIT, tid, buf[2]);
	} else {
		hist_entry ev (event::THRD_EXIT, tid);
		hist->second.push_back(ev);
	}
	// deregister thread with cds
	if (mt) cds::threading::Manager::detachThread();
}

// add an event to the calling thread's history
//
// note that in order for backtracing to work correctly, 
// this function should only be called by pthread_* methods
//
// other class methods have to add events directly
void tracer::add_event(event e, size_t obj_addr) {
	size_t tid = get_tid();
	hist_map::iterator hist = histories.contains(tid);
	assert(hist != histories.end());
	hist_entry ev (e, obj_addr);
	hist->second.push_back(ev);
}

} // namespace lktrace
