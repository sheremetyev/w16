W16 - Concurrent V8
===================

This is an experiment in automatic parallelization of JavaScript execution. It
includes modified V8 and an execution environment for event-driven JavaScript
programs.

The idea is very simple. Instead of executing all events in one thread we could
run several threads (one per processor core) with event loops working on a
common event queue. Conflicting access to data can be detected using Software
Transactional Memory. Each event is a transaction. If it happens to be involved
in a conflict the event can be reverted and restarted.

The implementation of W16 is far from being complete. Some of the most effective
optimizations were disabled to simplify implementation. The goal of the
experiment was to show that combination of event-driven programming and STM
creates a scalable computational model.

We have just one computationally-intensive
[test](https://github.com/sheremetyev/w16/blob/master/w16/primes.js)
here. It counts number of primes under 10^6. The counting process is split into
batches and each batch is executed as an event. The number of primes is known so
we can easily verify the correctness of execution. Splitting the work into
events gives the parallelization engine an opportunity to distribute load
between processors.

Tests are reassurring. We have almost linear scalability.

Implementation Details
======================

There are following modification to V8 engine.
- thread management code is removed
- thread-specific data members are moved from Isolate to ThreadLocalTop
- some parts of the Heap are made thread-local
- functions have a separate version of compiled code for each thread with
  embedded pointers to thread-local instances of data
- shared data is protected by mutexes
- when GC is required all threads are paused and references in all threads are
  traversed
- simplified version of
  [commit-time invalidating STM](http://dl.acm.org/citation.cfm?id=1772970) is
  [implemented](https://github.com/sheremetyev/w16/blob/master/src/stm.cc)
- read and write operations on heap objects are intercepted and redirected via
  STM to a copy of an objects
- [event-driven execution environment](https://github.com/sheremetyev/w16/blob/master/w16/main.cc)
  is implemented

The [difference](https://github.com/sheremetyev/w16/compare/v8...master) from
the original V8 source code is kept as small as possible.

The following things are not implemented:
- mutual exclusion for external calls from JavaScript to C++ and marking of
  transaction as irrevocable
- deferred execution of asynchronous external calls

W16 can be compiled on Windows with Visual Studio 2010 and on Mac OS X with
Xcode 4.2. Other configurations haven't been tested.

Running Tests
=============

Windows
-------

Run the following command to build and execute W16 on Windows in 2 threads.

    generate.cmd
    build.cmd
    w16\Debug\w16 w16\primes.js --threads=2

Mac OS X
--------

Run the following command to build and execute W16 on Mac OS X in 2 threads.

    ./generate.sh
    ./build.sh
    w16/build/Debug/w16 w16/primes.js --threads=2

Should you have any problems with W16 please don't hesitate to
[contact me](sheremetyev@gmail.com).
