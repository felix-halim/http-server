case $1 in

	init)
		git submodule init
		git submodule update
		cd libuv
		git checkout v1.4.0
		;;

	build)
		gyp/gyp --depth=. -Ilibuv/common.gypi -Duv_library=static_library http_server.gyp -f make
		make BUILDTYPE=${2:-Release} # or Debug
		;;

	build_mac)
		gyp/gyp -Dtarget_arch=x64 --depth=. -Ilibuv/common.gypi -Duv_library=static_library http_server.gyp -f xcode
		xcodebuild -verbose -project http_server.xcodeproj -configuration Release -target All
		;;

	clean)
		rm -rf build out http_server.Makefile http_server.target.mk http_server.xcodeproj test_server.target.mk gyp-mac-tool Makefile
		;;

esac
