CXXFLAGS = -Wall

all: mpegdemux

main.o: main.cpp
	g++ -c $(CXXFLAGS) $<

options.o: options.cpp
	g++ -c $(CXXFLAGS) $<

buffer.o: buffer.cpp
	g++ -c $(CXXFLAGS) $<

mpegdemux: main.o options.o buffer.o
	g++ -o mpegdemux $^

clean:
	rm -vf mpegdemux *.o

rebuild: clean all

