// adapters for running this script under Node.js
print = (typeof print == 'undefined') ? console.log : print;
enqueue = (typeof enqueue == 'undefined') ? process.nextTick : enqueue;

function isPrime(n) {
    for (var i = 2; i*i <= n; i++) {
        if (n%i == 0) {
            return 0;
        }
    }
    return 1;
};

var FIRST = 2;
var LAST = 1000000;
var BATCH_SIZE = 1000;

var counters = {
    processed : 0,
    primes : 0,
};

function inc_primes(n) {
    counters.primes += n;
    return counters.primes;
}

function inc_processed(n) {
    counters.processed += n;
    return counters.processed;
}

// returns closure
function getPrintPrime(first, last) {
    return function () {
        var local_count = 0;
        for (var i = first; i < last; i++) {
            if (isPrime(i)) {
                local_count++;
            }
        }
        var global_count = inc_primes(local_count);
        if (inc_processed(last - first) == LAST - FIRST) {
            print(global_count + " primes.");
        };
    } 
};

for (var i = FIRST; i < LAST; i += BATCH_SIZE) {
    var first = i;
    var last = i + BATCH_SIZE;
    if (last > LAST) {
        last = LAST;
    }
    //print("enqueueing [" + first + ", " + last + ")");
    var closure = getPrintPrime(first, last);
    enqueue(closure);
};

print('Started.');

// http://primes.utm.edu/howmany.shtml
// pi(10000) = 1229
// pi(100000) = 9592
// pi(1000000) = 78498
// pi(10000000) = 664579
