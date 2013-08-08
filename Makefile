all: ./build ./test_server

./build:
	V=1 ./bin/gyp/gyp --depth=. -Goutput_dir=./out -Icommon.gypi --generator-output=./build -Dlibrary=static_library -f make

./test_server: test.cc
	make -C ./build/ test_server
	cp ./build/out/Release/test_server ./test_server

distclean:
	make clean
	rm -rf ./build

test:
	./build/out/Release/test_server

clean:
	rm -rf ./build/out/Release/obj.target/http_server/
	rm -f ./build/out/Release/http_server
	rm -f ./http_server
	rm -rf ./build/out

.PHONY: test

