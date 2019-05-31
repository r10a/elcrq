# ELCRQ

Simulation of three process roundtrip scenario using ELCRQ.

ELCRQ is a lockless block-when necessary queue based on [LCRQ](http://www.cs.tau.ac.il/~mad/publications/ppopp2013-x86queues.pdf).

The _block-when necessary_ behavior is so that the CPU is free to other things when the queue is empty. The main aim is to save power when the queue is empty. This is implemented using [Event-Count](http://www.1024cores.net/home/lock-free-algorithms/eventcounts)s.

Check implementation folders (c/c++) for build instructions.
