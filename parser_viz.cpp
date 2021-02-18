#include <ncurses.h>

namespace lktrace {

namespace viz {

#define QUIESC_COLOR 1
#define BLOCKED_COLOR 2
#define ACQ_COLOR 3
#define INFO_COLOR 4

struct thrd_dat {
	static lktrace::parser* P;
	const int TL_HEIGHT = 4;
	
	const size_t tid;
	const std::vector<log_entry>* hist_p;
	size_t lbd;

	thrd_dat(const std::pair<size_t, std::vector<log_entry> >& entry) : tid(entry.first),
	hist_p(&P->thrd_hist.at(tid)) {
		lbd = 0;
		//assert(hist_p == &(P->thrd_hist.at(tid)));
	}

	void draw_timeline (const int y_root, const size_t l_ts, const int width,
			const unsigned tick, const int select) {
		
		const std::vector<log_entry>& hist = *hist_p;	
		const size_t r_ts = l_ts + tick*width;

		// get requested timestamp range
		if (hist[lbd].ts < l_ts) {
			do {++lbd;} while (hist[lbd].ts < l_ts);
			--lbd;
		} else {
			while (hist[lbd].ts > l_ts && lbd > 0) --lbd;
		}

		// bold everything if this timeline is selected
		if (select >= 0) attron(A_BOLD);

		// clear old lines, draw bottom line
		for (int x = 0; x < width; ++x) {
			mvaddch(y_root+1, x, ' ');
			mvaddch(y_root+2, x, ' ');
			mvaddch(y_root+3, x, ACS_HLINE);
		}

		// print thread label
		mvprintw(y_root, 0, "[Thread %zx %s]", tid, P->thrd_hooks[tid].c_str());

		// go through ticks
		size_t c = lbd; // index to the next event to happen
		int xput = 0;
		for (size_t t = l_ts; t <= r_ts; t += tick, ++xput) {
			
			if (c < hist.size() && t >= hist[c].ts) ++c;

			// print event label if this is the selected tick
			if (select == xput && c > 0) {

				// print caller name, event, & object addr
				mvaddch(y_root + 2, xput, ACS_UARROW);
				std::stringstream label;
				label << P->caller_names[hist[c-1].caller] << ": " <<
					ev_code_to_str(hist[c-1].ev) << " 0x" << std::hex << hist[c-1].obj; 
				// flip label if it's too close to the edge
				if (label.str().size() + 1 > width - xput) {
					mvaddstr(y_root + 2, xput - label.str().size(), label.str().c_str());
				} else {
					mvaddstr(y_root + 2, xput + 1, label.str().c_str());
				}
			}

			// print timeline tick
			if (c == 0) mvaddch(y_root + 1, xput, ' ');
			else switch(hist[c-1].ev) {
				// blocking
				case (event::LOCK_REQ):
				case (event::COND_WAIT):
					mvaddch(y_root + 1, xput, '=' | COLOR_PAIR(BLOCKED_COLOR));
					break;
				// holding lock
				case (event::LOCK_ACQ):
					mvaddch(y_root + 1, xput, '=' | COLOR_PAIR(ACQ_COLOR));
					break;
				// terminated
				case (event::THRD_EXIT):
					mvaddch(y_root + 1, xput, ' ');
					break;	
				// normal operation
				default:
					mvaddch(y_root + 1, xput, '=' | COLOR_PAIR(QUIESC_COLOR));
			}
		}

		if (select >= 0) attroff(A_BOLD);
	}

};

lktrace::parser* thrd_dat::P = nullptr;

} // namespace lktrace::viz

#define DEFAULT_TICK 100000
const std::pair<size_t, const std::string>
human_readable_ns(size_t a) { // print a time in ns in human-readable units
	if (a > pow(10, 9)) 
		return std::make_pair(a/pow(10, 9), "s");
	else if (a > pow(10,6))
		return std::make_pair(a/pow(10, 6), "ms");
	else if (a > pow(10,3))
		return std::make_pair(a/pow(10, 3), "us");
	else
		return std::make_pair(a, "ns");
}

int parser::viz() {
	// set up ncurses
	initscr();
	assert(has_colors());
	start_color();
	cbreak();
	noecho();
	keypad(stdscr, true);
	int xmax, ymax;
	getmaxyx(stdscr, ymax, xmax);
	unsigned tick = DEFAULT_TICK;
	size_t l_ts = 0;
	use_default_colors(); // sets pair -1 to default fg/bg color
	init_pair(QUIESC_COLOR, COLOR_GREEN, -1);
	init_pair(BLOCKED_COLOR, COLOR_RED, -1);
	init_pair(ACQ_COLOR, COLOR_YELLOW, -1);
	init_pair(INFO_COLOR, COLOR_BLUE, -1);

	// set up thread display objects
	unsigned xsel = 0; // selected tick (maps to coordinate)
	unsigned ysel = 0; // selected thread (maps to thread index)
	size_t thrd_count = thrd_hist.size();
	viz::thrd_dat::P = this;
	// construct display objects
	std::vector<viz::thrd_dat> disp_thrds;
	for (auto& H : thrd_hist) disp_thrds.emplace_back(H);
	// the top and bottom threads currently displayed (bot_thr points after)
	size_t top_thr = 0;
	const unsigned num_slots = (ymax - 1) / 4; // top line is infobox
	size_t bot_thr = (num_slots > disp_thrds.size()) ? disp_thrds.size() : num_slots;

	// main command loop
	bool quit = false;
	while (!quit) {
		// redraw stuff
		for (size_t i = top_thr; i < bot_thr; ++i) {
			const int y_root = i*4 + 1;
			if (i == ysel) disp_thrds[i].draw_timeline(y_root, l_ts, xmax, tick, xsel);
			else disp_thrds[i].draw_timeline(y_root, l_ts, xmax, tick, -1);
		}
		auto tick_h = human_readable_ns(tick);
		auto lb_h = human_readable_ns(l_ts);
		mvprintw(0, 0, "lt=%zu %s, tick=%zu %s",
			lb_h.first, lb_h.second.c_str(), tick_h.first, tick_h.second.c_str());
		mvchgat(0, 0, -1, A_BOLD, INFO_COLOR, NULL);
		// put cursor on selection
		move(ysel*4 + 2, xsel);
		refresh();

		// get command
		int c = getch();
		switch(c) {
		case 'q':
			quit = true;
			break;
		case KEY_UP:
			if (ysel > 0) --ysel;
			if (ysel < top_thr) {
				--top_thr;
				--bot_thr;
			}
			break;
		case KEY_DOWN:
			if (ysel < disp_thrds.size()-1) ++ysel;
			if (ysel >= bot_thr) {
				++top_thr;
				++bot_thr;
			}
			break;
		case KEY_RIGHT:
			if (xsel < xmax-1) ++xsel;
			else l_ts += tick;
			break;
		case KEY_LEFT:
			if (xsel > 0) --xsel;
			else if (l_ts > tick) l_ts -= tick;
			else if (l_ts > 0) l_ts = 0;
			break;
		case KEY_SRIGHT: // 10x move
			if (xsel < xmax-11) xmax += 10;
			else if (xsel < xmax-1) {
				l_ts += tick*(10 -(xmax-1-xsel));
				xsel = xmax - 1;
			}
			else l_ts += tick*10;
			break;
		case KEY_SLEFT:
			if (xsel >= 10) xsel -= 10;
			else {
				if (l_ts > tick*(10 - xsel)) l_ts -= tick*(10 - xsel);
				else l_ts = 0;
				xsel = 0;
			}
			break;
		case '=': // zoom in
			tick /= 2;
			break;
		case '-': // zoom out
			tick *= 2;
			break;
		}

	}

	endwin();

	return 0;
}

} // namespace lktrace
