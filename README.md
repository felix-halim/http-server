Simple HTTP Server
===========

A simple HTTP server in C++ built on top of 
[libuv](https://github.com/joyent/libuv)
and
[http-parser](https://github.com/joyent/http-parser)
.

To compile:

    make

To run:

    ./test_server

Then go to these URLs:


* [http://localhost:8000/add/10/1,2,3](http://localhost:8000/add/10/1,2,3)

    The response should be: hello 60
 
* [http://localhost:8000/add/1000/1,2,3](http://localhost:8000/add/1000/1,2,3)

    The response should be: {"error":"Internal Server Error"}

* [http://localhost:8000/add/1000/1,2,x3](http://localhost:8000/add/1000/1,2,x3)

    The response should be: {"error":"URL Request Error"}


See <b>test.cc</b> on how to use.


<b>Note</b>: this project is just for trying out things.
It is not tested at all! <b>Use at your own risk</b>.
