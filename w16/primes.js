// adapters for execution under Node.js
async = typeof async != 'undefined' ? async : process.nextTick;
print = typeof print != 'undefined' ? print : console.log;

function isPrime(n) {
  for (var i = 2; i*i <= n; i++) {
    if (n % i == 0)
      return 0;
  }
  return 1;
};

var counters = {
  processed : 0,
  primes : 0
};

function inc_primes(n) {
  counters.primes += n;
  return counters.primes;
}

function inc_processed(n) {
  counters.processed += n;
  return counters.processed;
}

// returns a closure
function searchPrimes(first, last) {
  return function () {
    var local_count = 0;
    for (var i = first; i < last; i++) {
      if (isPrime(i))
        local_count++;
    }
    var global_count = inc_primes(local_count);
    if (inc_processed(last - first) == LAST - FIRST)
      print(global_count + " primes.");
  }
};

var FIRST = 2;
var LAST = 1000000;
var BATCH = BATCH_PARAM || 1000;

for (var i = FIRST; i < LAST; i += BATCH) {
  var first = i;
  var last = i + BATCH;
  if (last > LAST)
    last = LAST;
  var closure = searchPrimes(first, last);
  async(closure);
};

// http://primes.utm.edu/howmany.shtml
// pi(10000) = 1229
// pi(100000) = 9592
// pi(1000000) = 78498
// pi(10000000) = 664579
