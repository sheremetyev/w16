#!/bin/sh

for threads in 1 2 3 4 5 6 7
do
  for batch in 10 50 100 500 1000 5000 10000 50000 100000 5000000
  do
    build/Debug/w16 $threads primes.js $batch
  done
done
