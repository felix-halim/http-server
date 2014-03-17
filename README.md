Simple HTTP Server
===========

A simple HTTP server in C++ built on top of 
[libuv](https://github.com/joyent/libuv)
and
[http-parser](https://github.com/joyent/http-parser)
.

To compile on OSX:

    ./run.sh build_mac

To run the server on OSX:

    ./build/Release/test_server


Then go to these URLs using your browser:


* [http://localhost:8000/add/2,3](http://localhost:8000/add/2,3)

    The response should be: a + b = 5
 
* [http://localhost:8000/add_async/5,3](http://localhost:8000/add_async/5,3)

    The browser should hang (i.e., not responded immediately).
    Then open another tab and open this link:

* [http://localhost:8000/add_flush](http://localhost:8000/add_flush)

    The response should be: "flushed 1 requests" and
    the previous tab should also have responded.


Or access the server using the client code:

    ./build/Release/test_client "/add/1,4"
    ./build/Release/test_client "/add_async/8,11"
    ./build/Release/test_client "/add_flush"


See <b>[test_server.cc](https://github.com/felix-halim/http-server/blob/master/test_server.cc)</b> for the server code.
See <b>[test_client.cc](https://github.com/felix-halim/http-server/blob/master/test_client.cc)</b> for the client code.
The client tries to reconnect if connection to the server is failing.

<b>Note</b>: this project is just for trying out things.
It is not tested at all! <b>Use at your own risk</b>.
