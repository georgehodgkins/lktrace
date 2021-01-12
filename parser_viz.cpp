#include <ncurses.h>

namespace lktrace {

namespace viz {

struct thrd_dat {
	static lktrace::parser* P;
	static int y_scale;
	static int y_root_assign;
	
	const size_t tid;
	const std::vector<log_entry>* hist_p;
	const int y_root;
	size_t lbd;

	thrd_dat(const std::pair<size_t, std::vector<log_entry> >& entry) : tid(entry.first),
	hist_p(&P->thrd_hist.at(tid)), y_root(y_root_assign) {
		y_root_assign += y_scale;
		lbd = 0;
		//assert(hist_p == &(P->thrd_hist.at(tid)));
	}

	void draw_timeline (const size_t l_ts, const size_t width, const unsigned tick) {
		const std::vector<log_entry>& hist = *hist_p;	
		const size_t r_ts = l_ts + tick*width;

		// zone requested range
		if (hist[lbd].ts < l_ts) {
			do {++lbd;} while (hist[lbd].ts < l_ts);
			--lbd;
		} else {
			while (hist[lbd].ts > l_ts && lbd > 0) --lbd;
		}

		size_t c = lbd;
		int xput = 0;
		for (size_t t = l_ts; t <= r_ts; t += tick, ++xput) {
			// clear old lines
			for (int x = 0; x < width; ++x) {
				mvaddch(y_root+2, x, ' ');
				if (y_scale > 4) mvaddch(y_root+3, x, ' ');
			}
			if (c+1 < hist.size() && t >= hist[c+1].ts) { // event transition
				++c;
				int p = 0;
				if (y_scale > 4) {
					mvprintw(y_root + 2, xput, "|@ %s", 
						P->caller_names[hist[c].caller].c_str());
					p = 1;
				}
				mvprintw(y_root + p+2, xput, "%s: %zx",
					ev_code_to_str(hist[c].ev).c_str(),
					hist[c].obj);
			}
			switch(hist[c].ev) {
				// blocking
				case (event::LOCK_REQ):
				case (event::COND_WAIT):
					mvaddch(y_root + 1, xput, '-');
					break;
				// holding lock
				case (event::LOCK_ACQ):
					mvaddch(y_root + 1, xput, '=' | A_BOLD);
					break;
				// terminated
				case (event::THRD_EXIT):
					break;	
				// normal operation
				default:
					mvaddch(y_root + 1, xput, '=');
			}
		}
	}

};

int thrd_dat::y_scale = 0;
int thrd_dat::y_root_assign = 0;
lktrace::parser* thrd_dat::P = nullptr;

} // namespace lktrace::viz

#define DEFAULT_TICK 1024

int parser::viz() {
	// set up display
	initscr();
	cbreak();
	noecho();
	keypad(stdscr, true);
	int xmax, ymax;
	getmaxyx(stdscr, ymax, xmax);
	unsigned tick = DEFAULT_TICK;
	size_t l_ts = 0;

	// set up thread display
	size_t thrd_count = thrd_hist.size();
	viz::thrd_dat::P = this;
	if (ymax % thrd_count) {
		viz::thrd_dat::y_root_assign = ymax % thrd_count;
		viz::thrd_dat::y_scale = ymax / thrd_count;
	} else {
		viz::thrd_dat::y_root_assign = (ymax-1) % thrd_count;
		viz::thrd_dat::y_scale = (ymax-1) / thrd_count;
	}
	std::vector<viz::thrd_dat> disp_thrds;
	for (auto& H : thrd_hist) disp_thrds.emplace_back(H);

	// write initial descriptors & timelines
	for (auto& T : disp_thrds) {
		mvprintw(T.y_root, 0, "Thread %zx %s:",
				T.tid, thrd_hooks[T.tid].c_str());
		T.draw_timeline(l_ts, xmax-1, tick);
	}

	mvprintw(0, 0, "lt=%llu, tick=%u", l_ts, tick);
	refresh();

	bool quit = false;
	while (!quit) {
		int c = getch();
		switch(c) {
		case 'q':
			quit = true;
			break;
		case KEY_LEFT:
			l_ts = (l_ts > tick) ? l_ts - tick : 0;
			break;
		case KEY_RIGHT:
			l_ts += tick;
			break;
		case '=': // zoom in
			tick /= 2;
			break;
		case '-': // zoom out
			tick *= 2;
			break;
		}

		for (auto& T: disp_thrds) T.draw_timeline(l_ts, xmax-1, tick);
		mvprintw(0, 0, "lt=%llu, tick=%u", l_ts, tick);
		refresh();
	}

	endwin();

	return 0;
}

} // namespace lktrace
