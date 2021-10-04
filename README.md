# LruClockCache
Low-latency LRU approximation cache in C++ using CLOCK second-chance algorithm.
```CPP
using MyKeyType = std::string;
using MyValueType = MinecraftChunk;

LruClockCache<MyKeyType,MyValueType> cache(1024*5,[&](MyKeyType key){ 
  // cache miss (read)
  // access data-store (network, hdd, graphics card, anything that is slower than RAM or higher-latency than RAM-latency x2)
  return readChunkFromHDD(key);
  },[&](MyKeyType key,MyValueType value){ 
  
  // cache miss (write)
  // access data-store
  writeChunkToHDD(key,value);
});

MyValueType val = cache.get("a key");
cache.set("another key",val);
cache.flush(); // clears all pending-writes in the cache and writes to backing-store
```

# Benchmarks:
Lowest latency performance with char key, char value: <b>27 nanoseconds</b>

Highest cache-miss bandwidth performance with 100-character std::string key, 100-character std::string value: <b>1.9 GB/s (~21% peak bw)</b>

Highest cache-hit bandwidth performance with 100-character std::string key, 100-character std::string value: <b>4.9 GB/s (~55% peak bw)</b>

Test system: FX8150 CPU @ 3.6GHz (CPU from 2011), 1-channel DDR3 RAM @ 1600 MHz (9GB/s peak bw and 150ns latency), Ubuntu 18.04 LTS 64-bit

Test method for cache-miss: single-threaded simple loop doing get&set using 100k different keys & values with only 300 cache items

Test method for cache-hit: single-threaded simple loop doing get&set using 10 keys & 15 cache items
