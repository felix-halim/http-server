all:
	clang++ -O0 -g -stdlib=libc++ -std=gnu++11 test.cc http_server.cc -I./http-parser \
		-I./libuv/include http-parser/http_parser.c -L/usr/local/lib -framework CoreServices libuv/build/Release/libuv.a && ./a.out

# git submodule init
# git submodule update
