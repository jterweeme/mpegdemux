all:
	g++ -o main main.cpp

clean:
	rm -vf main

rebuild: clean all

