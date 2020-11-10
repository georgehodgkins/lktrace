CFLAGS = -g -O0 -Wall -Wextra -fPIC
DEPS = -lcds -ldl -pthread

all: pthread_trace.so lkdump

pthread_trace.so: pthread_trace.cpp hist.o tracer.o
	g++ $(CFLAGS) -shared -o $@ pthread_trace.cpp -Wl,--whole-archive \
	      	hist.o tracer.o -L. -pthread -Wl,--no-whole-archive $(DEPS)

lkdump: lkdump.cpp hist.o parser.o
	g++ $(CFLAGS) -o $@ $^

%.o: %.cpp
	g++ $(CFLAGS) -c -o $@ $^ $(DEPS)

clean:
	rm -f *.o *.so
