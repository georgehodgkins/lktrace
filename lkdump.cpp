#include "parser.h"
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <cassert>
#include "enum_ops.h"
int main (int argc, char** argv) {
	// initialize params
	std::string out_fname;
	enum CMD : char {CMD_NONE =0x0, CMD_THREADS = 0x1, CMD_OBJECTS = 0x2,
		CMD_PATTERNS = 0x4};
	CMD the_command = CMD_NONE;

	// setup options
	enum OPT_ID : int {OPT_OUTFILE = (int) 'o', OPT_THREADS, OPT_OBJECTS, OPT_PATTERNS};
	const option longopts[] = {
		{"threads", no_argument, nullptr, OPT_THREADS},
		{"objects", no_argument, nullptr, OPT_OBJECTS},
		{"patterns", no_argument, nullptr, OPT_PATTERNS},
		{0, 0, 0, 0}};
	int opt;

	// get options
	while( (opt = getopt_long(argc, argv, "o:", longopts, nullptr)) != -1) {
		switch (opt) {
		case (OPT_OUTFILE):
			out_fname = optarg;
			break;
		case (OPT_THREADS):
			the_command |= CMD_THREADS;
			break;
		case (OPT_OBJECTS):
			the_command |= CMD_OBJECTS;
			break;
		case (OPT_PATTERNS):
			the_command |= CMD_PATTERNS;
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
	if (the_command & CMD_PATTERNS) {
		P.find_patterns();
		P.dump_patterns(outs);
	}
	if (the_command & CMD_OBJECTS) {
		std::cerr << "Object dump does not actually exist yet.";
	}


	if (file_out.is_open()) file_out.close();
	return 0;	
}
