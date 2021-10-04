# LruClockCache
A low-latency LRU approximation cache in C++ using CLOCK second-chance algorithm.
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
Lowest latency-performance with char key, char value: <b>27 nanoseconds</b>

Test system: FX8150 CPU @ 3.6GHz, 1-channel DDR3 RAM @ 1600 MHz, Ubuntu 18.04 LTS 64-bit
