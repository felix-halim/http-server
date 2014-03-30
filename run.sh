case $1 in

	init)
		git submodule init
		git submodule update
		;;

	build)
		gyp/gyp --depth=. -Ilibuv/common.gypi -Duv_library=static_library http_server.gyp -f make
		make
		;;

	build_mac)
		gyp/gyp --depth=. -Ilibuv/common.gypi -Duv_library=static_library http_server.gyp -f xcode
		xcodebuild -verbose -project http_server.xcodeproj -configuration Release -target All
		;;

	clean)
		rm -rf build out http_server.Makefile http_server.target.mk http_server.xcodeproj test_server.target.mk gyp-mac-tool Makefile
		;;

esac
