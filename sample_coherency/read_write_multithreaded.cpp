#include "DirectMappedMultiThreadCache.h"
#include "NWaySetAssociatedMultiThreadCache.h"
#include <vector>
#include <omp.h>
#include <iostream>

int main(int argC, char ** argV)
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


int main2()
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
