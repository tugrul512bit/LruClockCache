#include "DirectMappedMultiThreadCache.h"
#include <vector>
#include <omp.h>
#include <iostream>

int main(int argC, char ** argV)
{
        // ok to access different indices multithreaded
	std::vector<int> backingStore(100000);

	// single-level cache is inherently coherent with just using setThreadSafe getThreadSafe methods
	// direct mapped cache has low hit ratio. LruClockCache is better but it has only 1 synchronization point (whole LRU is locked)
	DirectMappedMultiThreadCache<int,int> cache(10,
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
