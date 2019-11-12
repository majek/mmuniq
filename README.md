mmuniq
======

Streaming tool to filter out duplicate lines. "uniq" implemented with
bloom filter.

Compile:

    make

Run:

    ./mmuniq --help
    Usage: ./mmuniq < input-file > output-file
    Filters duplicate lines using probabilistic bloom filter.
    Reads newline-delimited data from STDIN, writes unique lines
    to STDOUT.
    
      -n   Estimated cardinality of input. Default 500000000
      -p   Desired false positive probability. Default 0.000100
      -k   Number of hash functions to use. Default 8
      -q   Quiet.
      -D   Print all repeated lines.


Example run:

    $ echo -en "a\nb\nc\na\nb\nc\n"|./mmuniq -n 200
    [.] Bloom parameters: n=200 p=0.000100 k=8 m=0 MiB
    a
    b
    c

You should choose `n` to be roughly cardinality of your input data
set. Overshooting it is fine, undershooting will affect false positive
rate. The `p` is a desired false positive rate. Default value of
0.0001 means that if a row hashes are set in the bloom filter, it has
1 in 10000 chance of being a false positive - not a real duplicate.
Higher `p` increases chance lines will be missing from the output, but
decreases memory usage. The number of hashes `k` is number of hashes,
and also memory-hits per row. For performance reasons keep it a small
value like 4, 8 or 12. Larger `k` may mean lower memory usage.

Example run. For file with 66M lines, and 38 total lines:

    $ wc -l testcase.txt
    66224612 testcase.txt

    $ sort -u testcase.txt|wc -l
    38441637

Consider the following runs:

    # default parameters
    $ ./mmuniq < testcase.txt | wc -l
    [.] Bloom parameters: n=500000000 p=0.000100 k=8 m=1254 MiB
    38441637

    # assuming cardinality of 67M
    $ ./mmuniq -n 67000000 < testcase.txt | wc -l
    [.] Bloom parameters: n=67000000 p=0.000100 k=8 m=168 MiB
    38441626

    # cardinality of 67M but lower fp rate
    $ ./mmuniq -n 67000000 -p 0.0000001 < testcase.txt | wc -l
    [.] Bloom parameters: n=67000000 p=0.000000 k=8 m=446 MiB
    38441637

    $ ./mmuniq -n 38000000 < testcase.txt | wc -l
    [.] Bloom parameters: n=38000000 p=0.000100 k=8 m=95 MiB
    38441056

    $ ./mmuniq -n 38000000 -p 0.0000001 < testcase.txt | wc -l
    [.] Bloom parameters: n=38000000 p=0.000000 k=8 m=253 MiB
    38441637

You can observe the vastly different memory requirements and error
counts. With correctly adjusted `n` (at 38M) we get 581 rows
misclassified as duplicates, giving false positive rate of 0.00001511,
almost exactly as desired.
