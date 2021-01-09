#include "tracer.h"
#include "getopt.h"

// defined globally so we can access it in signal handler
unsigned int instance_ctr = 0;

void sigchld_handler (int signum, siginfo_t *info, void*) {
	assert(signum = SIGCHLD);
	if (info->si_code == CLD_KILLED || info->si_code == CLD_DUMPED)
		--instance_ctr;
}
	
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
	
	// attach SIGCHLD handler to keep instance counter accurate
	struct sigaction handler;
	handler.sa_sigaction = &sigchld_handler;
	handler.sa_flags = SA_SIGINFO | SA_NOCLDSTOP | SA_NOCLDWAIT;
	int e = sigaction(SIGCHLD, &handler, NULL);

	// set up instance counting socket & epoll set
	int instance_sock = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	sockaddr_un sock_addr;
	sock_addr.sun_family = AF_UNIX;
	strcpy(sock_addr.sun_path, "/tmp/lktracesock");
	e = bind(instance_sock, (sockaddr*) &sock_addr, sizeof(sockaddr_un));
	int sock_poll = epoll_create1(EPOLL_CLOEXEC);
	assert(sock_poll != -1);
	epoll_event sock_ev;
	sock_ev.events = EPOLLIN;
	sock_ev.data.fd = instance_sock;
	e = epoll_ctl(sock_poll, EPOLL_CTL_ADD, instance_sock, &sock_ev);

	// set up control structure in shared memory
	int ctl_fd = shm_open("/lktracectl", O_CREAT | O_EXCL | O_RDWR, S_IWUSR | S_IRUSR);
	assert(ctl_fd != -1);
	e = ftruncate(ctl_fd, sizeof(lktrace::tracer_ctl)); // TODO: dynamic size
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
		// TODO: handle launch failure in parent (abort?)
		perror("exec");
		assert(false && "Failure to launch target!");
	}

	free(so_path);
	free(target_path);

	// wait for all tracer instances to terminate
	// the sockets are never actually used to transmit data,
	// a new connection indicates a new tracer instance
	// and a hangup on the connection indicates a tracer instance has been destroyed
	epoll_event events[16];
	e = listen(instance_sock, 16);
	assert(e == 0);
	int ev_count = 0;
	do {
		if (ev_count == 0) // skip the poll if events were found in the previous nonblocking check
			ev_count = epoll_wait(sock_poll, events, 16, -1);
		for (int i = 0; i < ev_count; ++i) {
			if (events[i].data.fd == instance_sock) { // new connection (tracer instance)
				assert(events[i].events & EPOLLIN);
				int new_conn = accept(instance_sock, NULL, NULL);
				assert(new_conn != -1);
				++instance_ctr;
				epoll_event new_conn_ev;
				new_conn_ev.data.fd = new_conn;
				new_conn_ev.events = EPOLLRDHUP;
				epoll_ctl(sock_poll, EPOLL_CTL_ADD, new_conn, &new_conn_ev);
			} else {
				assert(events[i].events & EPOLLRDHUP);
				epoll_ctl(sock_poll, EPOLL_CTL_DEL, events[i].data.fd, NULL);
				--instance_ctr;
				close(events[i].data.fd);
			}
		}
		// do a nonblocking check in case events came in during the loop
		ev_count = epoll_wait(sock_poll, events, 16, 0);
	} while (instance_ctr > 0 || ev_count > 0);

	// clean up IPC
	e = munmap(ctl, sizeof(lktrace::tracer_ctl));
	assert(e == 0);
	e = close(sock_poll);
	assert(e == 0);
	e = close(instance_sock);
	assert(e == 0);
	e = unlink("/tmp/lktracesock");
	assert(e == 0);
	e = shm_unlink("/lktracectl");
	assert(e == 0);

	return 0;
}
