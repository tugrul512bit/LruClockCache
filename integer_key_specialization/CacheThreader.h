/*
 * CacheThreader.h
 *
 *  Created on: Oct 7, 2021
 *      Author: root
 */

#ifndef CACHETHREADER_H_
#define CACHETHREADER_H_


#include<vector>
#include<memory>
#include<thread>
#include<atomic>
#include"DirectMappedCache.h"
#include"../LruClockCache.h"
/* L1: direct mapped cache, for each thread
 * L2: LRU clock cache, for each thread (size must be integer-power of 2)
 * LLC: user-defined cache with thread-safe get/set methods that is slower but global
 * currently only 1 thread is supported
*/
template<template<typename,typename, typename> class Cache,typename CacheKey, typename CacheValue, typename CacheInternalCounterTypeInteger=size_t>
class CacheThreader
{
private:
	// last level cache, slow because of lock-guard
	std::shared_ptr<Cache<CacheKey,CacheValue,CacheInternalCounterTypeInteger>> LLC;
	std::shared_ptr<LruClockCache<CacheKey,CacheValue,CacheInternalCounterTypeInteger>> L2;
	std::shared_ptr<DirectMappedCache<CacheKey,CacheValue>> L1;


public:
	CacheThreader(std::shared_ptr<Cache<CacheKey,CacheValue,CacheInternalCounterTypeInteger>> cacheLLC, int sizeCacheL1, int sizeCacheL2)
	{

		LLC=cacheLLC;
		// backing-store of L1 is LLC
			L2=std::make_shared<LruClockCache<CacheKey,CacheValue,CacheInternalCounterTypeInteger>>(sizeCacheL2,[this](CacheKey key){

				return this->LLC->getThreadSafe(key);
			},[this](CacheKey key, CacheValue value){

				this->LLC->setThreadSafe(key,value);
			});
			L1=std::make_shared<DirectMappedCache<CacheKey,CacheValue>>(sizeCacheL1,[this](CacheKey key){

				return this->L2->get(key);
			},[this](CacheKey key, CacheValue value){

				this->L2->set(key,value);
			});
	}

	// get data from closest cache
	// currently only 1 thread supported
	const CacheValue get(CacheKey key) const
	{
		return L1->get(key);
	}

	// set data to closest cache
	// currently only 1 thread supported
	void set(CacheKey key, CacheValue value) const
	{
		L1->set(key,value);
	}

	// currently only 1 thread supported for read+write
	// only read-only usage for multi-threaded apps
	// must be called from all threads
	// does not flush LLC
	// LLC needs to be flushed manually by main-thread
	void flush()
	{
		L1->flush();
		L2->flush();
	}

	~CacheThreader(){  }
};


#endif /* CACHETHREADER_H_ */
