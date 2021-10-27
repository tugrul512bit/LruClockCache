/*
 * MultiLevelCache.h
 *
 *  Created on: Oct 27, 2021
 *      Author: root
 */

#ifndef MULTILEVELCACHE_H_
#define MULTILEVELCACHE_H_

#include "integer_key_specialization/DirectMappedMultiThreadCache.h"
#include "integer_key_specialization/NWaySetAssociativeMultiThreadCache.h"

//	integer key type, any value type, thread-safe, read-write coherent, multi-level cache that is made of
//		direct mapped (+sharded) L1 cache as front-end and n-way set-associative LRU approximation (+sharded) cache as back-end
//	single instance can be used directly from multiple threads without extra initialization
template<typename CacheKey=size_t, typename CacheValue=size_t>
class MultiLevelCache
{
public:
	// by default, 64k L1 tags + 256k L2 tags
	MultiLevelCache(const std::function<CacheValue(CacheKey)> & readCacheMiss, const std::function<void(CacheKey,CacheValue)> & writeCacheMiss):
		L2(256,1024, readCacheMiss, writeCacheMiss),
		L1(1024*64,[this](CacheKey key){ return this->L2.get(key); },[this](CacheKey key, CacheValue value){ this->L2.set(key,value); })
	{

	}

	// L1size = number of tags in L1 (has to be power of 2)
	// L2sets = number of sets in L2 (has to be power of 2)
	// L2tagsPerSet = number of tags in each set
	// L2size = L2sets * L2tagsPerSet
	MultiLevelCache(size_t L1size, size_t L2sets, size_t L2tagsPerSet,const std::function<CacheValue(CacheKey)> & readCacheMiss, const std::function<void(CacheKey,CacheValue)> & writeCacheMiss):
		L2(L2sets,L2tagsPerSet, readCacheMiss, writeCacheMiss),
		L1(L1size,[this](CacheKey key){ return this->L2.get(key); },[this](CacheKey key, CacheValue value){ this->L2.set(key,value); })
	{

	}

	inline
	CacheValue get(const CacheKey & key) noexcept
	{
		return L1.get(key);
	}

	inline
	CacheValue getThreadSafe(const CacheKey & key) noexcept
	{
		return L1.getThreadSafe(key);
	}

	inline
	void set(const CacheKey & key, const CacheValue & value) noexcept
	{
		L1.set(key,value);
	}

	inline
	void setThreadSafe(const CacheKey & key, const CacheValue & value) noexcept
	{
		L1.setThreadSafe(key,value);
	}

	// call before shutting program/connection down and after all read/write of other threads are complete
	void flush()
	{
		L1.flush();
		L2.flush();
	}
private:
	NWaySetAssociativeMultiThreadCache<CacheKey,CacheValue> L2;
	DirectMappedMultiThreadCache<CacheKey,CacheValue> L1;

};


#endif /* MULTILEVELCACHE_H_ */
