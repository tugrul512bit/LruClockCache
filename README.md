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

// cache handles all cace-miss functions automatically
MyValueType val = cache.get("a key");

// cache handles all cace-miss functions automatically
cache.set("another key",val);

cache.flush(); // clears all pending-writes in the cache and writes to backing-store
```

# Benchmarks:

Lowest cache-miss latency performance with char key, char value: <b>27 nanoseconds</b>

Lowest cache-hit latency performance with char key, char value: <b>16 nanoseconds</b>

Highest cache-miss bandwidth performance with very long std::string key, very long std::string value: <b>1.9 GB/s (~21% peak bw)</b>

Highest cache-hit bandwidth performance with very long std::string key, very long std::string value: <b>4.9 GB/s (~55% peak bw)</b>

<b>Test system: FX8150 CPU @ 3.6GHz (CPU from 2011), 1-channel DDR3 RAM @ 1600 MHz (9GB/s peak bw and 150ns latency), Ubuntu 18.04 LTS 64-bit</b>

<b>Test method for cache-miss: single-threaded simple loop doing get&set using 100k different keys & values with only 300 cache items</b>

<b>Test method for cache-hit: single-threaded simple loop doing get&set using 10 keys & 15 cache items</b>

-----

<b>Image Softening Algorithm:</b> read each pixel and its 4 closest neighbor pixels and write to same pixel the average of 5 pixels

Image size = 1024x1024 pixels

Cache size = 1024x5 pixels (1024x1024 pixels)

Repeats: 10 (10)

Key: pixel's x y values stringified and joined together (same)

Value: 32-bit integer (same)

Total time: 22.5 seconds (13.7 seconds)

Cache time: 21.6 seconds (13.6 seconds)

Throughput = <b>2.9 million pixel get/set operations per second (4.6 million pixels per second ✅ )</b>

Cache-hit-ratio (read): <b>78% (100%)</b>
