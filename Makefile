all: ./build ./test_server

./build:
	git submodule init
	git submodule update
	V=1 ./bin/gyp/gyp --depth=. -Goutput_dir=./out -Icommon.gypi --generator-output=./build -Dlibrary=static_library -f make

./test_server: test.cc
	make -C ./build/ test_server
	cp ./build/out/release/test_server ./test_server

distclean:
	make clean
	rm -rf ./build

clean:
	rm -rf ./build/out/release/obj.target/test_server/
	rm -f ./build/out/release/test_server
	rm -f ./test_server

.PHONY: test

