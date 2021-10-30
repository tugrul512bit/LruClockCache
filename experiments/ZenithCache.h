/*
 * ZenithCache.h
 *
 *  Created on: Oct 30, 2021
 *      Author: tugrul
 */

#ifndef ZENITHCACHE_H_
#define ZENITHCACHE_H_

#include "../AsyncCache.h"
#include "../integer_key_specialization/DirectMappedCacheShard.h"
#include<vector>
#include<memory>

template<typename CacheKey, typename CacheValue>
class ZenithCache
{
public:
	ZenithCache(const size_t L1tags, const size_t L2tags, const int shards,
			const std::function<CacheValue(CacheKey)> & readCacheMiss,
			const std::function<void(CacheKey,CacheValue)> & writeCacheMiss): numShards(shards), numShardsM1(shards-1)
	{
		for(int i=0;i<numShards;i++)
		{

			shard.push_back(
				std::make_shared<	AsyncCache<CacheKey,CacheValue,DirectMappedCacheShard<CacheKey,CacheValue>>  >(
					L1tags/numShards,
					L2tags/numShards,
					readCacheMiss,
					writeCacheMiss,
					1,
					numShards,
					i
				)

			);

			shardPtr.push_back(shard[i].get());
		}
	}

	// asynchronously get the value by key, return slot id for current operation
	inline
	void getAsync(const CacheKey & key, CacheValue * valPtr) const
	{
		shardPtr[key&numShardsM1]->getAsync(key,valPtr,0);
	}

	// asynchronously set the value by key, return slot id for current operation
	inline
	void setAsync(const CacheKey & key, const CacheValue & val) const
	{
		shardPtr[key&numShardsM1]->setAsync(key,val,0);
	}

	// asynchronously flush cache, slot number not important
	inline
	void flush() const
	{
		for(int i=0;i<numShards;i++)
		{
			shardPtr[i]->flush();
		}
	}

	// wait for read/write operations on a slot to complete
	inline
	void barrier() const
	{
		for(int i=0;i<numShards;i++)
		{
			shardPtr[i]->barrier(0);
		}
	}

private:
	const int numShards;
	const int numShardsM1;
	std::vector<std::shared_ptr<AsyncCache<CacheKey,CacheValue,DirectMappedCacheShard<CacheKey,CacheValue>>>> shard;
	std::vector<AsyncCache<CacheKey,CacheValue,DirectMappedCacheShard<CacheKey,CacheValue>> *> shardPtr;
};



#endif /* ZENITHCACHE_H_ */
