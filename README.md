W16 - Concurrent V8
===================

This is an experiment in automatic parallelization of JavaScript execution. It
includes modified Google’s V8 and an execution environment for event-driven
JavaScript programs. The project is based on the work I did for [MSc thesis][0]
at Oxford University.

[0]: <https://raw.github.com/sheremetyev/w16/master/thesis.pdf>

The idea is very simple. Instead of executing all events in one thread we could
run several threads (one per processor core) with event loops working on a
common event queue. Conflicting access to data can be detected using [software
transactional memory][1]. Each event is a transaction. If it happens to be
involved in a conflict the event can be reverted and restarted.

[1]: <http://en.wikipedia.org/wiki/Software_transactional_memory>

The implementation of W16 is far from being complete. Some of the most effective
optimizations were disabled to simplify implementation. The goal of the
experiment was to show that combination of event-driven programming and STM
creates a scalable computational model.

We have just one computationally-intensive [test][2]. It counts number of primes
under 10^6. The counting process is split into batches and each batch is
executed as an event. The number of primes is known so we can easily verify the
correctness of execution. Splitting the work into events gives the
parallelization engine an opportunity to distribute load between processors.

[2]: <https://github.com/sheremetyev/w16/blob/master/w16/primes.js>

In my experiments (on AWS High-CPU Extra-Large instance with Amazon Linux 64
bit) W16 scaled almost linearly. The exection times were as following.

-   1 thread: 24.5 seconds

-   2 threads: 12.3 seconds

-   3 threads: 8.5 seconds

-   4 threads: 6.5 seconds

Implementation Details
======================

There are following modification to V8 engine.

-   thread management code is removed

-   thread-specific data members are moved from Isolate to ThreadLocalTop

-   some parts of the Heap are made thread-local

-   functions have a separate version of compiled code for each thread with
    embedded pointers to thread-local instances of data

-   shared data is protected by mutexes

-   when GC is required all threads are paused and references in all threads are
    traversed

-   simplified version of  [commit-time invalidating STM][3] is [implemented][4]

[3]: <http://dl.acm.org/citation.cfm?id=1772970>

[4]: <https://github.com/sheremetyev/w16/blob/master/src/stm.cc>

-   read and write operations on heap object are intercepted and redirected via
    STM to a copy of the object; changes are written back to the original object
    on commit

-   [event-driven execution environment][5]  is implemented

[5]: <https://github.com/sheremetyev/w16/blob/master/w16/main.cc>

The [difference][6] from the original V8 source code is kept as small as
possible.

[6]: <https://github.com/sheremetyev/w16/compare/v8...master>

The following things are not implemented.

-   mutual exclusion for external calls from JavaScript to C++ and marking of
    transaction as irrevocable

-   deferred execution of asynchronous external calls

W16 can be compiled on Windows with Visual Studio 2010 and on Mac OS X with
Xcode 4.2. Other configurations haven’t been tested.

Compiling and Testing
=====================

Windows
-------

Run the following commands to clone, build and execute W16 on Windows (in 2
threads).

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
git clone git://github.com/sheremetyev/w16.git
cd w16
svn export http://src.chromium.org/svn/trunk/deps/third_party/cygwin@66844 third_party/cygwin
generate.cmd
build.cmd
w16\Debug\w16 w16\primes.js --threads=2
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The last command should produce the following output.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
[Worker 0] 78498 primes.
2 threads, 15282 ms, 10 transactions, 0 aborts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Mac OS X
--------

Run the following commands to clone, build and execute W16 on Mac OS X.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
git clone git://github.com/sheremetyev/w16.git
cd w16
./generate.sh
./build.sh
w16/build/Debug/w16 w16/primes.js --threads=2
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Linux
-----

Run the following commands to clone, build and execute W16 on Linux.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
git clone git://github.com/sheremetyev/w16.git
cd w16
./generate.sh
make -f Makefile-ia32
out/Debug/w16 w16/primes.js --threads=2
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Should you have any questions about W16 please don’t hesitate to [contact
me][7].

[7]: <mailto:sheremetyev@gmail.com>
