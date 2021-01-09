#include "tracer.h"
#include "addr2line.h" // avoid multiple defns

namespace lktrace {

/*---------------------------class tracer-----------------------------*/
	
size_t tracer::get_tid () { // an alias for pthread_self, cast to size_t
	return (size_t) pthread_self();
}

// callback to find the .so after ours in the list (ie get our own address range)
// we pass in the start address in the data field, 
// the end address is returned in data
// TODO: use link map instead?
int phdr_callback (dl_phdr_info *info, size_t sz, void* data) {
	static size_t prev = 0;
	if (info->dlpi_addr == *(size_t*) data) *(size_t*) data = prev;
	prev = (size_t) info->dlpi_addr;
	return 0;
}
	
tracer::tracer() : 
	histories(MAX_THRD_COUNT, 1), 
	init_time(chrono::steady_clock::now()),
	init_guard(false) {
		// register this tracer instance with the master
		instance_sock = socket(AF_UNIX, SOCK_STREAM, 0);
		sockaddr_un addr;
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, "/tmp/lktracesock");
		int e = connect(instance_sock, (sockaddr*) &addr, sizeof(sockaddr_un));
		assert(e == 0);

		// get tracer options from shared mem
		int ctl_fd = shm_open("/lktracectl", O_RDONLY, S_IRUSR);
		assert(ctl_fd != -1);
		struct stat info;
		e = fstat(ctl_fd, &info);
		assert(e == 0);
		void* ctl_v = mmap(NULL, (size_t) info.st_size, PROT_READ, MAP_SHARED, ctl_fd, 0);
	       	assert(ctl_v != MAP_FAILED);
		ctl = (tracer_ctl*) ctl_v;
		e = close(ctl_fd); // don't need fd after mapping
		assert(e == 0);
		
		// set up cds thread tracking
		cds::Initialize();
		// find beginning and end of our own .so
		find_obj_bounds((void*) &addr2line,
				hist_entry::start_addr, hist_entry::end_addr);
		// find beginning and end of allocator .so
		find_obj_bounds((void*) &malloc,
				hist_entry::alloc_start, hist_entry::alloc_end);
		// set trace skip
		hist_entry::trace_skip = ctl->trace_skip;
		// register master thread
		void* buf[2];
		e = backtrace(buf, 2);
		assert(e == 2);
		add_this_thread(0, buf[1],  false);
		// this assignment must be deferred to here
		// because the first run (only) of backtrace calls the allocator
		init_guard = true;
}

// dump backtrace before termination
// (catch exception throws in multi-process programs)
void ahnold () {
	void* buf[15];
	int ul = backtrace(buf, 15);
	for (int i = 0; i < ul; ++i) 
		std::cerr << std::hex << (size_t) buf[i] << ": " << addr2line((size_t) buf[i]) << '\n';
	if (ul == 15)
		std::cerr << "<more frames may exist>";
	std::abort();
}

tracer::~tracer () { // purpose of this destructor is to write out our results
	// register C++ termination handler
	set_terminate(&ahnold);
	// add thread exit event for master, but don't deregister w/cds
	sever_this_thread(false);

	if (multithreaded) { // don't write anything out if there was never >1 thread

	// when we find a dscriptor string for an address using addr2line, we store it here
	std::unordered_map<size_t, std::string> caller_name_cache;

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
		auto h_it = caller_name_cache.find((size_t) hist.front().addr);
		if (h_it == caller_name_cache.end()) {
			std::string name = (hist.front().addr == 0) ? "<program entry point>"
				: addr2line((size_t) hist.front().addr);	
			auto emplit = caller_name_cache.insert(
				std::make_pair((size_t) hist.front().addr, name));
			assert(emplit.second);
		}
		
		outfile << '[';
		if (tid == get_tid()) outfile << "t";
		else outfile << "t"; 
		outfile << ":0x" << hex << tid << ":0x" << hist.front().addr << "]\n";
		for (hist_entry& entry : hist) {
			// write timestamp
			outfile << dec << (entry.ts - init_time).count() << ':';
			// write event code
			outfile << ev_code_to_str(entry.ev);
			// write object & caller addrs
			outfile << ":0x" << hex << entry.addr
				<< ":0x" << (size_t) entry.caller << '\n';

			// look up and cache caller name if not already present
			auto call_it = caller_name_cache.find((size_t) entry.caller);
			if (call_it == caller_name_cache.end()) {
				std::string name = addr2line((size_t) entry.caller);	
				caller_name_cache.insert(
					std::make_pair((size_t) entry.caller, name));
			}

		}
		outfile << '\n';
	}
	// write out cached caller names
	outfile << "[n:]\n";
	for (auto caller : caller_name_cache) {
		outfile << "0x" << caller.first << ':' << caller.second << '\n';
	}
	outfile << '\n';

	addr2line_cache_cleanup(); // close opened object files
	outfile.close();
	} // if (multithreaded)

	// clean up cds
	cds::threading::Manager::detachThread();
	cds::Terminate();

	// deregister tracer instance with master
	close(instance_sock);
}

void tracer::add_this_thread(size_t hook, void* caller, bool mt) {
	assert(init_guard || !mt);
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
	assert(init_guard);
	// add thread exit event
	size_t tid = get_tid();
	hist_map::iterator hist = histories.contains(tid);
	assert(hist != histories.end());
	void* buf[3];
	int e = backtrace(buf, 3);
	assert(e == 3);
	hist->second.emplace_back(event::THRD_EXIT, tid, buf[2]);
	// deregister thread with cds
	if (mt) cds::threading::Manager::detachThread();
}

// add an event to the calling thread's history
// hist_entry ctor gets timestamp & stack trace
void tracer::add_event(event e, size_t obj_addr) {
	if (!init_guard) return;
	size_t tid = get_tid();
	hist_map::iterator hist = histories.contains(tid);
	assert(hist != histories.end());
	// the hist_entry ctor will throw std::bad_alloc if the caller
	// appears to be the memory allocator (we get infinite recursion otherwise)
	// if this occurs we continue silently (not an error, as such)
	try {
		hist_entry ev (e, obj_addr);
		hist->second.push_back(ev);
	} catch (std::bad_alloc& e) {}	
}

/*-----------------class hist_entry----------------------------*/


// define static vars
size_t hist_entry::start_addr = 0;
size_t hist_entry::end_addr = 0;
size_t hist_entry::alloc_start = 0;
size_t hist_entry::alloc_end = 0;
unsigned int hist_entry::trace_skip = 0;

// how many frames up to look for calling code
#define TRACE_DEPTH 8

hist_entry::hist_entry(event e, size_t obj_addr) : 
	ts(chrono::steady_clock::now()), ev(e), addr(obj_addr) {

	// these are set in the tracer ctor	
	assert(start_addr && end_addr);
	assert(alloc_start && alloc_end);
		
	void* buf[TRACE_DEPTH];
	int v = backtrace(buf, TRACE_DEPTH);
	int a = 0;
	// find first frame outside of our own code
	while (a < v && start_addr < (size_t) buf[a] &&
		end_addr > (size_t) buf[a]) ++a;
	// skip requested amount of frames
	a += trace_skip;
	if (a >= v) a = v-1;
	caller = buf[a];

	if ((size_t) caller > alloc_start &&
			(size_t) caller < alloc_end) throw std::bad_alloc();
}

// ctor overload to manually set caller addr rather than looking it up
// used when spawning threads; obviously, we can't stack trace
// from within a thread to the code that created it
hist_entry::hist_entry(event e, size_t obj_addr, void* c) :
	ts(chrono::steady_clock::now()), ev(e), caller(c), addr(obj_addr) {}

} // namespace lktrace

