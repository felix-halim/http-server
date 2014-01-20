Simple HTTP Server
===========

A simple HTTP server in C++ built on top of 
[libuv](https://github.com/joyent/libuv)
and
[http-parser](https://github.com/joyent/http-parser)
.

To compile on OSX:

    ./run.sh build_mac

To run on OSX:

    ./run.sh run_test_mac

Then go to these URLs:


* [http://localhost:8000/add/2,3](http://localhost:8000/add/2,3)

    The response should be: a + b = 5
 
* [http://localhost:8000/add_async/5,3](http://localhost:8000/add_async/5,3)

    The browser should hang (i.e., not responded immediately).
    Then open another tab and open this link:

* [http://localhost:8000/add_flush](http://localhost:8000/add_flush)

    The response should be: "flushed 1 requests" and
    the previous tab should also have responded.


See <b>test.cc</b> on how to use.


<b>Note</b>: this project is just for trying out things.
It is not tested at all! <b>Use at your own risk</b>.
