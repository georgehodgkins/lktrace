#include "parser.h"
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <cassert>
#include "enum_ops.h"
int main (int argc, char** argv) {
	// initialize params
	std::string out_fname;
	size_t min_depth = 0;
	enum CMD : char {CMD_NONE =0x0, CMD_THREADS = 0x1, CMD_PATTERNS = 0x2,
		CMD_PATTERNS_TXT = 0x4, CMD_GLOBAL = 0x8};
	CMD the_command = CMD_NONE;

	// setup options
	enum OPT_ID : int {OPT_OUTFILE = (int) 'o', OPT_DEPTH = (int) 'd',
		OPT_THREADS, OPT_PATTERNS, OPT_PATTERNS_TXT, OPT_GLOBAL};
	const option longopts[] = {
		{"threads", no_argument, nullptr, OPT_THREADS},
		{"patterns", no_argument, nullptr, OPT_PATTERNS},
		{"patterns-text", no_argument, nullptr, OPT_PATTERNS_TXT},
		{"global", no_argument, nullptr, OPT_GLOBAL},
		{0, 0, 0, 0}};
	int opt;

	// get options
	while( (opt = getopt_long(argc, argv, "o:d:", longopts, nullptr)) != -1) {
		switch (opt) {
		case (OPT_OUTFILE):
			out_fname = optarg;
			break;
		case (OPT_THREADS):
			the_command |= CMD_THREADS;
			break;
		case (OPT_PATTERNS):
			the_command |= CMD_PATTERNS;
			break;
		case (OPT_PATTERNS_TXT):
			the_command |= CMD_PATTERNS_TXT;
			break;
		case (OPT_DEPTH):
			min_depth = atoi(optarg);
			break;
		case (OPT_GLOBAL):
			the_command |= CMD_GLOBAL;
			break;
		default:
			assert(false && "Default block in option parsing reached!");
		}
	}

	// validate options
	if (the_command == CMD_NONE) {
		std::cerr << "Must specify at least one command.\n";
		return 1;
	}
	std::ofstream file_out;
	if (!out_fname.empty()) {
		file_out.open(out_fname);
		if (!file_out.is_open()) {
			std::cerr << "Could not open output file " << out_fname << ".\n";
			return 1;
		}
	}
	std::ostream& outs =
		(file_out.is_open()) ? (std::ostream&) file_out : (std::ostream&) std::cout;
	if (optind == argc) {
		std::cerr << "Must specify a trace file to parse.\n";
		return 1;
	} else if (argc - optind > 1) {
		std::cerr << "Only one trace file at a time.\n";
		return 1;
	}

	// run commands
	lktrace::parser P (std::string(argv[optind]));
	if (the_command & CMD_THREADS) {
		P.dump_threads(outs);
	}
	if (the_command & CMD_PATTERNS_TXT) {
		P.find_patterns();
		P.dump_patterns_txt(outs, min_depth);
	}
	if (the_command & CMD_PATTERNS) {
		P.find_deps(min_depth);
		P.dump_patterns(outs);
	}
	if (the_command & CMD_GLOBAL) {
		P.dump_global(outs);
	}


	if (file_out.is_open()) file_out.close();
	return 0;	
}
