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

    [http://localhost:8000/add/10/1,2,3](http://localhost:8000/add/10/1,2,3)
    [http://localhost:8000/add/1000/1,2,3](http://localhost:8000/add/1000/1,2,3)
    [http://localhost:8000/add/1000/1,2,x3](http://localhost:8000/add/1000/1,2,x3)

The first URL should give:

    hello 60

The second URL should give:

    {"error":"Internal Server Error"}

The first URL should give:

    {"error":"URL Request Error"}

See test.cc on how to use.


<b>Note</b>: this project is just for trying out things.
It is not tested at all! <b>Use at your own risk</b>.
