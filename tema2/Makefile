server:
	g++ -Wall server.cpp common.cpp -o server

subscriber:
	g++ -Wall subscriber.cpp common.cpp -o subscriber

all:
	make clean
	make server
	make subscriber

clean:
	rm -r -f subscriber server