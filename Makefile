CXXFLAGS = -Wall

all: mpegdemux

main.o: main.cpp
	g++ -c $(CXXFLAGS) main.cpp

options.o: options.cpp
	g++ -c $(CXXFLAGS) options.cpp

mpegdemux: main.o options.o
	g++ -o mpegdemux main.o options.o

clean:
	rm -vf mpegdemux *.o

rebuild: clean all

