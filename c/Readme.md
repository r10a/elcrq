## ELCRQ C implementation

Simulation of three process roundtrip scenario using ELCRQ.

Shared memory module from [shm_malloc](https://github.com/ChrisDodd/shm_malloc) library.

ELCRQ is a lockless block-when necessary queue based on [LCRQ](http://www.cs.tau.ac.il/~mad/publications/ppopp2013-x86queues.pdf).

The _block-when necessary_ behavior is so that the CPU is free to other things when the queue is empty. The main aim is to save power when the queue is empty. This is implemented using [Event-Count](http://www.1024cores.net/home/lock-free-algorithms/eventcounts)s.

Default simulation runs with 6 threads per process, i.e., 18 threads total, each pinned to different core.
Hence, the processor should have at least 18 cores to run this simulation.

Definitions in `main.c`:
- `NUM_THREAD` : number of threads in each process to run the simulation with.
- `NUM_ITERS` : number of time to run the simulation.
- `NUM_RUNS` : number of elements to enqueue/dequeue in each simulation.
- `SHM_FILE` : name of file to use for shared memory(created automatically).

Definitions in `ELCRQ.h`:
- `RING_POW` : The ELCRQ's ring size will be `2^{RING_POW}`.
- `MAX_PATIENCE` : maximum number of times to retry before giving up in `spinDequeue`.
- `Object` : Type of elements in the queue.

### Build instructions

1. Clone repositroy using `git clone https://github.com/r10a/elcrq`
2. cd `elcrq/c`
3. Build: `cmake . && make`
4. Run `./c`