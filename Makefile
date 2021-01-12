CFLAGS = -g -O0 -Wall -Wextra -fPIC 
DEPS = -lcds -ldl -lbfd -lrt -lncurses -pthread

all: pthread_trace.so lkdump lktrace
	rm -f core

pthread_trace.so: tracer.o addr2line.h tracer.cpp
	g++ $(CFLAGS) -shared -o $@ pthread_trace.cpp -Wl,--whole-archive \
	      	tracer.o -L. -pthread -Wl,--no-whole-archive $(DEPS)

lktrace: pthread_trace.so lktrace.cpp
	g++ $(CFLAGS) -o $@ lktrace.cpp tracer.o $(DEPS)

lkdump: lkdump.cpp parser.o
	g++ $(CFLAGS) -o $@ $^ $(DEPS)

tracer.o: tracer.cpp
	g++ $(CFLAGS) -c -o $@ $^ $(DEPS)

parser.o: parser.cpp parser_viz.cpp
	g++ $(CFLAGS) -c -o $@ parser.cpp $(DEPS)

clean:
	rm -f *.o *.so
