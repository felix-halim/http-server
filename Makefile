darwin:
	clang++ -O0 -g -stdlib=libc++ -std=gnu++11 test.cc http_server.cc -I./http-parser \
		-I./libuv/include http-parser/http_parser.c -L/usr/local/lib -framework CoreServices /usr/local/lib/libuv.a && ./a.out

all:
	g++ -O0 -g -std=c++11 test.cc http_server.cc -I./http-parser \
		-I./libuv/include http-parser/http_parser.c -L/usr/local/lib -lrt /usr/local/lib/libuv.a && ./a.out

submodules:
	git submodule init
	git submodule update

