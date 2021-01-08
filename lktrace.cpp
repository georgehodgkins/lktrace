#include "tracer.h"
#include "getopt.h"
	
int main (int argc, char** argv) {
	// initialize params
	std::string prefix = "lktracedat";
	unsigned int trace_skip = 0;
	
	// setup options
	enum OPT_ID: int {OPT_PREFIX = (int) 'f', OPT_FSKIP = (int) 'd'};
	const option longopts[] = {
		{"prefix", required_argument, nullptr, OPT_PREFIX},
		{"skip-frames", required_argument, nullptr, OPT_FSKIP},
		{0, 0, 0, 0}};
	int opt;

	// get options
	while ( (opt = getopt_long(argc, argv, "f:d:", longopts, nullptr)) != -1) {
		switch (opt) {
		case (OPT_PREFIX):
			prefix = optarg;
			break;
		case (OPT_FSKIP):
			trace_skip = atoi(optarg);
			break;
		default:
			assert(false && "Default block in option parsing reached!");
		}
	}

	prefix += '-';
	
	// set up instance semaphore
	sem_t *instance_sem = sem_open("/lktraceinst", O_CREAT | O_EXCL, S_IWUSR | S_IRUSR, 0);
	assert(instance_sem != SEM_FAILED);

	// set up control structure in shared memory
	int ctl_fd = shm_open("/lktracectl", O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR);
	assert(ctl_fd != -1);
	int e = ftruncate(ctl_fd, sizeof(lktrace::tracer_ctl)); // TODO: dynamic size
	assert(e == 0);
	void* ctl_v = mmap(NULL, sizeof(lktrace::tracer_ctl), PROT_READ | PROT_WRITE, MAP_SHARED, ctl_fd, 0);
	assert(ctl_v != MAP_FAILED);
	lktrace::tracer_ctl* ctl = new (ctl_v) lktrace::tracer_ctl;
	ctl->trace_skip = trace_skip;
	close(ctl_fd); // don't need this after mmap

	// set up execution environment for target
	char* so_path = realpath("pthread_trace.so", NULL);
	assert(so_path != NULL);
	e = setenv("LD_PRELOAD", so_path, 1);
	assert(e == 0);
	char* target_path = realpath(argv[optind], NULL);
	assert(target_path != NULL);
	char* last_sep = strrchr(target_path, '/');
	assert(last_sep != NULL);
	*last_sep = '\0';
	e = chdir(target_path);
	assert(e == 0);
	*last_sep = '/';

	// fork off target executable
	pid_t child = fork();
	if (child == 0) {
		execvp(target_path, &argv[optind+1]);
		sem_post(instance_sem); // avoid hang in parent process
		perror("exec");
		assert(false && "Failure to launch target!");
	}

	free(so_path);
	free(target_path);

	// wait for all tracer instances to terminate
	// TODO: this is a bit of a kludge
	sem_wait(instance_sem);
	int sval = -1;
	do {
		const timespec sleepy = {5, 0};
		nanosleep(&sleepy, NULL);
		sem_getvalue(instance_sem, &sval);
		assert(sval >= 0);
	} while (sval > 0);

	// clean up IPC
	e = munmap(ctl, sizeof(lktrace::tracer_ctl));
	assert(e == 0);
	e = sem_close(instance_sem);
	assert(e == 0);
	e = shm_unlink("/lktracectl");
	assert(e == 0);
	e = sem_unlink("/lktraceinst");
	assert(e == 0);

	return 0;
}
