# LruClockCache
Low-latency LRU approximation cache in C++ using CLOCK second-chance algorithm. (see [wiki](https://github.com/tugrul512bit/LruClockCache/wiki) for details)

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
MinecraftChunk chunk = cache.get("world coordinates 1500 35 2000");

// cache handles all cace-miss functions automatically
cache.set("world coordinates 1502 35 1999",chunk);

cache.flush(); // clears all pending-writes in the cache and writes to backing-store
```

<b>160x speedup on noise-generation for procedural terrain (2x speedup against AVX/SIMD optimized version):</b>

[![160x speedup on noise-generation for procedural terrain](https://i.snipboard.io/iAXH5Z.jpg)](https://www.youtube.com/watch?v=Sw8fh3c7ESQ "160x speedup on noise-generation for procedural terrain! (click to watch video)")




# Benchmarks:

# LRU-Clock Cache:

Up to <b>50 million lookups per second</b> on an old CPU like FX8150 under heavy get/set usage.

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

Cache size = 1024x5 pixels (1024x1024+1000 pixels)

Repeats: 10 (50)

Key: 32-bit integer (same)

Value: 32-bit integer (same)

Total time: 10.4 seconds (6.7 seconds)

Cache time: 9.6 seconds (6.25 seconds)

Throughput = <b>6.5 million pixel get/set operations per second (~50 million pixels per second)</b>

Cache-hit-ratio (read): <b>78% (100%)</b>

Timings (50 million pixel lookups per second = 20 nanosecond average per access) include all of the computation work: integer division after pixel summations and chrono time measurement latency.

--------
# Multi Level Cache (read+write coherency + multithreaded = 75 million lookups per second)

```CPP
int main()
{
	std::vector<int> data(1024*1024); // simulating a backing-store
    
	MultiLevelCache<int,int> cache(
		64*1024 /* direct-mapped L1 cache elements */,
		256,1024 /* n-way set-associative (LRU approximation) L2 cache elements */,
		[&](int key){ return data[key]; } /* cache-miss function to get data from backingstore into cache */,
		[&](int key, int value){ data[key]=value; } /* cache-miss function to set data on backging-store during eviction */
	);
	cache.set(5,10); // this is single-thread example, sets value 10 at key position of 5
	cache.flush(); // writes all latest bits of data to backing-store
	std::cout<<data[5]<<std::endl;
	auto val = cache.getThreadSafe(5); // this is thread-safe from any number of threads
	cache.setThreadSafe(10,val); //    thread-safe, any number of threads
	return 0;
}
```
--------
# Async Multi Level Cache (read+write weak-coherency(threads are responsible to use barrier) + multithread = up to 180 million lookups per second)

```CPP
int main()
{
	std::vector<int> data(1000000); // backing-store simulation
	
	// L1 cache = direct mapped 128 elements
	// L2 cache = fully associative 128*1024 elements
	// similar cache-miss functions with MultiLevelCache
	AsyncCache<int,int> cache(128,128*1024,[&](int key){ return data[key]; },[&](int key, int value){ data[key]=value; });
	
	int val;
	
	// immediately returns, cache runs asynchronously to serve in a dedicated thread
	int slot = cache.setAsync(5,100); // or int slot = omp_get_thread_num(); cache.setAsync(5,100,slot);
	
	// immediately returns, any slot id can be used but a thread should use its own unique slot id on all operations for maximum performance
	cache.getAsync(5,&val,slot);	
	
	std::cout<<data[5]<<" "<<val<<std::endl;
	
	// waits for completion of operations issued into slot
	cache.barrier(slot); 
	
	std::cout<<data[5]<<" "<<val<<std::endl;
	
	// writes all dirty data to backing-store and waits for completion
	cache.flush(); 
	
	std::cout<<data[5]<<" "<<val<<std::endl;
	return 0;
}
```
--------

# Multi Level Cache (read-only + multithreaded = 2.5 billion lookups per second)

If keys are integer type (char, int, size_t), then a L1 direct-mapped cache can be added in front of an L2 LRU cache to act as a single-thread front of an LLC cache given by user (which implements thread-safe set/get operations). Currently it supports read-only multi-thread or read+write single thread:

```CPP
// simulating a backing-store
std::vector<int> backingStore(10000000);

// this is to be used by multi-level cache constructor and must implement getThreadSafe setThreadSafe methods.
auto LLC = std::make_shared<LruClockCache<int,int>>(LLCsize,
[ & ](int key) {
  readmiss++;
  return backingStore[key];
},
[ & ](int key, int value) {
  writemiss++;
  backingStore[key] = value;
});

// this is the multi-level cache that adds an L1 and an L2 infront of the LLC.
size_t L1size=1024*32; // this needs to be (power of 2) sized cache because it is direct-mapped (with N-way tags) cache
size_t L2size=1024*512; // this can be any size, it is LRU cache
CacheThreader<LruClockCache,int,int> cache(LLC,L1size,L2size);
cache.set(500,10); // if it is in L1 then returns directly in 1-2 nanoseconds
                    // if it is not in L1 then goes L2 and returns within ~50 nanoseconds if its an L2 hit
                    // if it is not L2 hit then it goes LLC in a thread-safe way and gets data much slower like 500 nanoseconds or more due to std::lock_guard
 
cache.get(500); // same as set method but returns a value

// currently multithreading not supported due to lack of write-invalidation method but with a few changes it is ready to be used as a read-only cache
// when invalidation method is implemented, it will be multithreaded read-write cache. For now, it is single-thread read-write cache.

cache.flush(); // write latest bits of data to the LLC
LLC.flush(); // write latest bits of data to the backing store
```

# Benchmarks for Multi-Level Cache:

Up to <b>400 million lookups per second</b> for FX8150 3.6GHz in single-threaded Gaussian Blur algorithm. This is equivalent to <b>2.5 nanoseconds</b> average access latency per pixel. For a new CPU like Ryzen, it should be as fast as a billion lookups per second.

<b>~900 million get calls per second in read-only usage with multiple threads</b>: https://github.com/tugrul512bit/LruClockCache/wiki/How-To-Do-Multithreading-With-a-Read-Only-Multi-Level-Cache

```
LLC cache hit ratio (read)=1
LLC cache hit ratio (write)=1
image size=34x34 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 175530469 nanoseconds     (bandwidth = 1009.24 MB/s)      (throughput = 3.96 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=1.00
LLC cache hit ratio (write)=1.00
image size=68x68 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 198700826 nanoseconds     (bandwidth = 947.92 MB/s)      (throughput = 4.22 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=1.00
LLC cache hit ratio (write)=1.00
image size=136x136 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 192917413 nanoseconds     (bandwidth = 1005.22 MB/s)      (throughput = 3.98 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=1.00
LLC cache hit ratio (write)=1.00
image size=272x272 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 298379386 nanoseconds     (bandwidth = 654.78 MB/s)      (throughput = 6.11 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=1.00
LLC cache hit ratio (write)=1.00
image size=544x544 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 3387914152 nanoseconds     (bandwidth = 55.49 MB/s)      (throughput = 72.08 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=0.93
LLC cache hit ratio (write)=0.45
image size=1088x1088 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 4518605673 nanoseconds     (bandwidth = 41.76 MB/s)      (throughput = 95.78 nanoseconds per iteration) 
Finished!
LLC cache hit ratio (read)=0.89
LLC cache hit ratio (write)=0.12
image size=2176x2176 L1 tags = 65536 L2 tags=262144 LLC tags=1048576 performance: 5058006420 nanoseconds     (bandwidth = 37.38 MB/s)      (throughput = 107.02 nanoseconds per iteration) 
Finished!

```
