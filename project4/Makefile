all:
	g++ socks.cpp -o bin/socks

start:
	bin/socks &
stop:
	killall socks
restart:
	killall socks
	bin/socks &
