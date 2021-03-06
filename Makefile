CXXFLAGS = -Wall -O2

all: mpegdemux

main.o: main.cpp
	g++ -c $(CXXFLAGS) $<

options.o: options.cpp
	g++ -c $(CXXFLAGS) $<

buffer.o: buffer.cpp
	g++ -c $(CXXFLAGS) $<

common.o: common.cpp
	g++ -c $(CXXFLAGS) $<

mpegdemux: main.o options.o buffer.o common.o
	g++ -o mpegdemux $^

clean:
	rm -vf mpegdemux *.o

rebuild: clean all

