/*
 * NWaySetAssociativeMultiThreadCache.h
 *
 *  Created on: Oct 13, 2021
 *      Author: tugrul
 */

#ifndef NWAYSETASSOCIATIVEMULTITHREADCACHE_H_
#define NWAYSETASSOCIATIVEMULTITHREADCACHE_H_

#include<vector>
#include<memory>
#include<functional>
#include"LruClockCache.h"

/* N parallel LRU approximations (Clock Second Chance)
* Each with own mutex
* cache-coherent writes+reads as long as user-given cache-miss functions handle the synchronization on the backing store
* 				each key is guarded by its own mutex guard so it shouldn't be a problem if backing-store can do parallel operations on different keys
* 				if you need also the backing-store be thread-safe, then put a lock guard in the miss-functions
* numberOfSets = number of LRUs in parallel (has to be power of 2: 2,4,8,...16k,32k,64k,....1M,2M,....)
* numberOfTagsPerLRU = number of cache items per set (LRU Clock cache)
* 			total size of cache is (numberOfSets * numberOfTagsPerLRU) elements
* ClockHandInteger: just an optional optimization to reduce memory consumption when cache size is equal to or less than 255,65535,4B-1,...
*/

template<typename CacheKey, typename CacheValue, typename CacheHandInteger=size_t>
class NWaySetAssociativeMultiThreadCache
{
public:
	NWaySetAssociativeMultiThreadCache(size_t numberOfSets, size_t numberOfTagsPerLRU,
			const std::function<CacheValue(CacheKey)> & readMiss,
			const std::function<void(CacheKey,CacheValue)> & writeMiss):numSet(numberOfSets),numSetM1(numberOfSets-1),numTag(numberOfTagsPerLRU)
	{

		for(CacheHandInteger i=0;i<numSet;i++)
		{
			sets.push_back(std::make_shared<LruClockCache<CacheKey,CacheValue,CacheHandInteger>>(numTag,readMiss,writeMiss));
		}
	}

	// allocates 64k tags per set (1024 sets = 64M cache size)
	NWaySetAssociativeMultiThreadCache(size_t numberOfSets,
			const std::function<CacheValue(CacheKey)> & readMiss,
			const std::function<void(CacheKey,CacheValue)> & writeMiss):numSet(numberOfSets),numSetM1(numberOfSets-1),numTag(1024*64)
	{

		for(CacheHandInteger i=0;i<numSet;i++)
		{
			sets.push_back(std::make_shared<LruClockCache<CacheKey,CacheValue,CacheHandInteger>>(numTag,readMiss,writeMiss));
		}
	}

	const CacheValue get(CacheKey key) const noexcept
	{
		// select set
		CacheHandInteger set = key & numSetM1;
		return sets[set]->get(key);
	}

	void set(CacheKey key, CacheValue value) const noexcept
	{
		// select set
		CacheHandInteger set = key & numSetM1;
		sets[set]->set(key,value);
	}

	const CacheValue getThreadSafe(CacheKey key) const noexcept
	{
		// select set
		CacheHandInteger set = key & numSetM1;
		return sets[set]->getThreadSafe(key);
	}

	void setThreadSafe(CacheKey key, CacheValue value) const noexcept
	{
		// select set
		CacheHandInteger set = key & numSetM1;
		sets[set]->setThreadSafe(key,value);
	}

	void flush()
	{
		for(CacheHandInteger i=0;i<numSet;i++)
		{
			sets[i]->flush();
		}
	}

private:
	const CacheHandInteger numSet;
	const CacheHandInteger numSetM1;
	const CacheHandInteger numTag;
	std::vector<std::shared_ptr<LruClockCache<CacheKey,CacheValue,CacheHandInteger>>> sets;
};



#endif /* NWAYSETASSOCIATIVEMULTITHREADCACHE_H_ */
