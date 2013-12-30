case $1 in

	init)
		git submodule init
		git submodule update
		;;


	build)
		gyp/gyp --depth=. -Dlibrary=static_library http_server.gyp -f make
		make
		;;


	build_mac)
		gyp/gyp --depth=. -Dlibrary=static_library http_server.gyp -f xcode
		xcodebuild -project http_server.xcodeproj -configuration Release -target All
		;;


	clean)
		rm -rf build
		;;

esac
