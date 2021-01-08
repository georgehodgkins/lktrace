CFLAGS = -g -O0 -Wall -Wextra -fPIC 
DEPS = -lcds -ldl -lbfd -lrt -pthread

all: pthread_trace.so lkdump lktrace
	rm -f core

pthread_trace.so: pthread_trace.cpp tracer.o addr2line.h
	g++ $(CFLAGS) -shared -o $@ pthread_trace.cpp -Wl,--whole-archive \
	      	tracer.o -L. -pthread -Wl,--no-whole-archive $(DEPS)

lktrace: pthread_trace.so tracer.o lktrace.cpp
	g++ $(CFLAGS) -o $@ lktrace.cpp tracer.o $(DEPS)

lkdump: lkdump.cpp parser.o
	g++ $(CFLAGS) -o $@ $^

%.o: %.cpp
	g++ $(CFLAGS) -c -o $@ $^ $(DEPS)

clean:
	rm -f *.o *.so
