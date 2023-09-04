# url_filter

This is a demonstration of 16-bit binary fused filters applied to URL filtering.

Usage:

```
make
./index data/top-1m.csv 
```

Possible output:

```
$ ./index data/top-1m.csv 
Permission denied, xnu/kpc requires root privileges.
loaded 1000000 names
average length 23.1128 bytes/name

duplicated string reddog.microsoft.com
total volume 23112842 bytes
number of duplicates hashes 1
ratio of duplicates  hashes 0.000001

filter memory usage : 2261032 bytes (9.8 % of input)

filter memory usage : 18 bits/entry

false-positive rate 0.000010
Benchmarking queries:
binary_fuse16_contain          :   1.42 GB/s   22.4 Ma/s  44.60 ns/d 
Benchmarking construction speed
binary_fuse16_populate         :   1.15 GB/s   49.8 Ma/s  20.09 ns/d 
```