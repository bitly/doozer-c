doozer-c
========

async [doozerd](http://github.com/ha/doozerd) client library in C based on [buffered_socket](http://github.com/bitly/simplehttp/tree/master/buffered_socket)

`doozerd` is a consistent, distributed, data store for storing really important pieces of data.

`doozer-c` enables asynchronous communication with `doozerd` via a clean, simple, API.

it currently supports `SET`, `GET`, `DEL` and `STAT` verbs and handles communication and reconnection 
to multiple `doozerd` instances participating in a cluster.

dependencies
============

 * [buffered_socket](http://github.com/bitly/simplehttp/tree/master/buffered_socket)
 * [protobuf-c](http://code.google.com/p/protobuf-c/) tested w/ 0.15
 * [protobuf](http://code.google.com/p/protobuf/) tested w/ 2.4.1
 * [libevent](http://www.monkey.org/~provos/libevent/) requires 1.4.14b
 * [json-c](http://oss.metaparadigm.com/json-c/)
