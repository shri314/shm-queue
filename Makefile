a.out: Crowd.cpp
	g++ Crowd.cpp -lrt -pthread -std=c++17 -O3

clean:
	rm -f a.out
