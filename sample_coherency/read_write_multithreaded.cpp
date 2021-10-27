#include "../integer_key_specialization/DirectMappedMultiThreadCache.h"
#include "../integer_key_specialization/NWaySetAssociatedMultiThreadCache.h"
#include "../MultiLevelCache.h"
#include <vector>
#include <omp.h>
#include <iostream>

int main()
{
  // simulating something slower than RAM-access or something that doesn't fit RAM
  std::vector<std::string> database(1000);

  int L1tags=512;// power of 2
  int L2sets=128;// power of 2
  int L2tagsPerSet=1000;
  MultiLevelCache<int,std::string> cache(L1tags,L2sets,L2tagsPerSet,
    // read-miss
    [&](int key){ return database[key];},

    // write-miss
    [&](int key, std::string value){ database[key]=value;}
  );
  cache.set(500,"hello world"); // cached
  std::cout<<cache.get(500); // from cache
  auto val=cache.get(700); // from database
  auto fastVal=cache.get(700); // from cache
  cache.flush(); // all written data in database now

  // Coherent test
  #pragma omp parallel for
  for(int i=0;i<100;i++)
     cache.setThreadSafe(i,std::to_string(i));

  // "55", threadafe
  auto val=cache.getThreadSafe(55);

  // all dirty bits go to database
  cache.flush();

  
  return 0;
}

int main2(int argC, char ** argV)
{
        // ok to access different indices multithreaded
	std::vector<int> backingStore(100000);

	// single-level cache is inherently coherent with just using setThreadSafe getThreadSafe methods
	// direct mapped cache has low hit ratio. LruClockCache is better but it has only 1 synchronization point (whole LRU is locked)
	DirectMappedMultiThreadCache<int,int> cache(32 /* power of 2 */,
			[&](int key){ return backingStore[key]; },
			[&](int key, int value){ backingStore[key]=value; });

	#pragma omp parallel for
	for(int i=0;i<20;i++)
	{
		cache.setThreadSafe(i,i*2);
	}

	for(int i=0;i<20;i++)
	{
		std::cout<<cache.getThreadSafe(i)<<std::endl;
	}
	return 0;
}


int main3()
{
	SomeBackingStoreThatAllowsMultithreadedAccessToDifferentKeys backingStore;
	// has better hit-ratio than direct-mapped cache, still coherent, multithreaded
	// 1024: number of sets (any power-of-2)
	// cacheSize/1024: number of tags per set (1 set = 1 LruClockCache with cacheSize/1024 number of tags)
	NWaySetAssociativeMultiThreadCache <int, int > cache(1024,cacheSize/(1024),[ & ](int key) {
		// no two threads will read same key simultaneously here
		// safe
		return backingStore[key];
	},
	[ & ](int key, int value) {
		// no two threads will write to same key simultaneously here
		// safe
		backingStore[key] = value;
	});
	// call this in a thread
	cache.setThreadSafe(5,5);
	
	// call this from another thread
	auto val = cache.getThreadSafe(5);
	
	// the more threads, the better average latency per access
	auto val2 = cache.getThreadSafe(100);

	return 0;	
}
