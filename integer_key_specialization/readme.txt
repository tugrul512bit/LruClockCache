With integer keys, it is easier to do locally multiplex multiple RAM accesses because we can predict if two or more accesses can be neighbors. 

But global multiplexing would require more advanced features of C++ such as nano-coroutines but this would solve the "integer keys only" problem.

Example local multiplexing:

Layout in the buffers (mapped by unordered_map)

```
Cache-line width = 4
          cache line 0       cache line 1         cache line 2        cache line 3
index     0  1  2  3         4  5   6   7        8  9   10   11       12   13  14   15
          ^  ^ 2x data              ^ 1x data         1x data              3x data
           \/                      /
            \                    /
             \                 /
            "1x find"       "1x find"
                \          /   
                 \__      /     
                  \ \   /                   
user requests: key 0,1,6,10,12,13,14 = 4x unordered_map find operations, 7x data

multiplexing computation: 

cache-line selection = (key/width)
cache-lane selection = key%width  (if width is integer power of 2 then it is optimized by compiler into bit-wise operations)
So only 2 bit-wise operations per key could be pipelined with all other keys and should make no more than few nanoseconds latency per access while gaining multiple times the original bandwidth of singular key lookups.

Currently, DirectMappedCache class contains an array of tags to singular items. This makes good enough multiplexing performance on the input. It just takes a single "&" operation to know the target tag. But since it is not an efficient method, CacheThreader class adds LRU behind the direct mapped cache and the LRU cache uses a LLC cache (just another LRU but with synchronized get/set methods) which is connected to the real datastore. This way, latency is as low as 2 nanoseconds on average (or there is multiplexing of RAM fetching which hides the latencies and achieves inverse-throughput of 2 nanoseconds).

```
