## Shared list of Concurrent Ring Queues benchmark

Simulation of three process roundtrip scenario using SCRQ.

Shared memory module from [Boost](https://www.boost.org/doc/libs/1_63_0/doc/html/interprocess.html) library.

SCRQ is a lockless block-when necessary queue based on [LCRQ](http://www.cs.tau.ac.il/~mad/publications/ppopp2013-x86queues.pdf).

The _block-when necessary_ behavior is so that the CPU is free to other things when the queue is empty. The main aim is to save power when the queue is empty. This is implemented using [Event-Count](http://www.1024cores.net/home/lock-free-algorithms/eventcounts)s.

_PS. Work in progress_
