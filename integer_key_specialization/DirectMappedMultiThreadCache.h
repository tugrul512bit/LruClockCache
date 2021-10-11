/*
 * DirectMappedCache.h
 *
 *  Created on: Oct 8, 2021
 *      Author: root
 */

#ifndef DIRECTMAPPEDMULTITHREADCACHE_H_
#define DIRECTMAPPEDMULTITHREADCACHE_H_

#include<vector>
#include<functional>
#include<mutex>


/* Direct-mapped cache implementation with granular locking (per-tag)
 * Only usable for integer type keys and intended to be used as LLC(last level cache) for CacheThreader instances
 * 															to optimize contentions out in multithreaded read-only scenarios
 *	also can be used alone, as a read+write multi-threaded cache using getThreadSafe setThreadSafe methods but cache-hit ratio will not be good
 * CacheKey: type of key (only integers: int, char, size_t)
 * CacheValue: type of value that is bound to key (same as above)
 * InternalKeyTypeInteger: type of tag found after modulo operationa (is important for maximum cache size. unsigned char = 255, unsigned int=1024*1024*1024*4)
 */
template<	typename CacheKey, typename CacheValue, typename InternalKeyTypeInteger=size_t>
class DirectMappedMultiThreadCache
{
public:
	// allocates buffers for numElements number of cache slots/lanes
	// readMiss: 	cache-miss for read operations. User needs to give this function
	// 				to let the cache automatically get data from backing-store
	//				example: [&](MyClass key){ return redis.get(key); }
	//				takes a CacheKey as key, returns CacheValue as value
	// writeMiss: 	cache-miss for write operations. User needs to give this function
	// 				to let the cache automatically set data to backing-store
	//				example: [&](MyClass key, MyAnotherClass value){ redis.set(key,value); }
	//				takes a CacheKey as key and CacheValue as value
	// numElements: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	DirectMappedMultiThreadCache(InternalKeyTypeInteger numElements,
				const std::function<CacheValue(CacheKey)> & readMiss,
				const std::function<void(CacheKey,CacheValue)> & writeMiss):size(numElements),sizeM1(numElements-1),loadData(readMiss),saveData(writeMiss)
	{
		mut = std::vector<std::mutex>(numElements);
		// initialize buffers
		for(InternalKeyTypeInteger i=0;i<numElements;i++)
		{
			valueBuffer.push_back(CacheValue());
			isEditedBuffer.push_back(0);
			keyBuffer.push_back(CacheKey());
		}
	}



	// get element from cache
	// if cache doesn't find it in buffers,
	// then cache gets data from backing-store
	// then returns the result to user
	// then cache is available from RAM on next get/set access with same key
	inline
	const CacheValue get(const CacheKey & key)  noexcept
	{
		return accessDirect(key,nullptr);
	}

	// only syntactic difference
	inline
	const std::vector<CacheValue> getMultiple(const std::vector<CacheKey> & key)  noexcept
	{
		const int n = key.size();
		std::vector<CacheValue> result(n);

		for(int i=0;i<n;i++)
		{
			result[i]=accessDirect(key[i],nullptr);
		}
		return result;
	}


	// thread-safe but slower version of get()
	inline
	const CacheValue getThreadSafe(const CacheKey & key)  noexcept
	{
		return accessDirectLocked(key,nullptr);
	}

	// set element to cache
	// if cache doesn't find it in buffers,
	// then cache sets data on just cache
	// writing to backing-store only happens when
	// 					another access evicts the cache slot containing this key/value
	//					or when cache is flushed by flush() method
	// then returns the given value back
	// then cache is available from RAM on next get/set access with same key
	inline
	void set(const CacheKey & key, const CacheValue & val) noexcept
	{
		accessDirect(key,&val,1);
	}

	// thread-safe but slower version of set()
	inline
	void setThreadSafe(const CacheKey & key, const CacheValue & val)  noexcept
	{
		accessDirectLocked(key,&val,1);
	}

	// use this before closing the backing-store to store the latest bits of data
	void flush()
	{
		try
		{
		for (CacheKey i=0;i<size;i++)
		{
		  if (isEditedBuffer[i] == 1)
		  {
				isEditedBuffer[i]=0;
				auto oldKey = keyBuffer[i];
				auto oldValue = valueBuffer[i];
				saveData(oldKey,oldValue);
		  }
		}
		}catch(std::exception &ex){ std::cout<<ex.what()<<std::endl; }
	}

	// CLOCK algorithm with 2 hand counters (1 for second chance for a cache slot to survive, 1 for eviction of cache slot)
	// opType=0: get
	// opType=1: set
	CacheValue const accessDirectLocked(const CacheKey & key,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		InternalKeyTypeInteger tag = key & sizeM1;
		std::lock_guard<std::mutex> lg(mut[tag]); // N parallel locks in-flight = less contention in multi-threading

		// compare keys
		if(keyBuffer[tag] == key)
		{
			// cache-hit

			// "set"
			if(opType == 1)
			{
				isEditedBuffer[tag]=1;
				valueBuffer[tag]=*value;
			}

			// cache hit value
			return valueBuffer[tag];
		}
		else // cache-miss
		{
			CacheValue oldValue = valueBuffer[tag];
			CacheKey oldKey = keyBuffer[tag];

			// eviction algorithm start
			if(isEditedBuffer[tag] == 1)
			{
				// if it is "get"
				if(opType==0)
				{
					isEditedBuffer[tag]=0;
				}

				saveData(oldKey,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(key);
					valueBuffer[tag]=loadedData;
					keyBuffer[tag]=key;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[tag]=*value;
					keyBuffer[tag]=key;
					return *value;
				}
			}
			else // not edited
			{
				// "set"
				if(opType == 1)
				{
					isEditedBuffer[tag]=1;
				}

				// "get"
				if(opType == 0)
				{
					const CacheValue && loadedData = loadData(key);
					valueBuffer[tag]=loadedData;
					keyBuffer[tag]=key;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[tag]=*value;
					keyBuffer[tag]=key;
					return *value;
				}
			}

		}
	}

	CacheValue const accessDirect(const CacheKey & key,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		InternalKeyTypeInteger tag = key & sizeM1;

		// compare keys
		if(keyBuffer[tag] == key)
		{
			// cache-hit

			// "set"
			if(opType == 1)
			{
				isEditedBuffer[tag]=1;
				valueBuffer[tag]=*value;
			}

			// cache hit value
			return valueBuffer[tag];
		}
		else // cache-miss
		{
			CacheValue oldValue = valueBuffer[tag];
			CacheKey oldKey = keyBuffer[tag];

			// eviction algorithm start
			if(isEditedBuffer[tag] == 1)
			{
				// if it is "get"
				if(opType==0)
				{
					isEditedBuffer[tag]=0;
				}

				saveData(oldKey,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(key);
					valueBuffer[tag]=loadedData;
					keyBuffer[tag]=key;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[tag]=*value;
					keyBuffer[tag]=key;
					return *value;
				}
			}
			else // not edited
			{
				// "set"
				if(opType == 1)
				{
					isEditedBuffer[tag]=1;
				}

				// "get"
				if(opType == 0)
				{
					const CacheValue && loadedData = loadData(key);
					valueBuffer[tag]=loadedData;
					keyBuffer[tag]=key;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[tag]=*value;
					keyBuffer[tag]=key;
					return *value;
				}
			}

		}
	}


private:
	const CacheKey size;
	const CacheKey sizeM1;
	std::vector<std::mutex> mut;

	std::vector<CacheValue> valueBuffer;
	std::vector<unsigned char> isEditedBuffer;
	std::vector<CacheKey> keyBuffer;
	const std::function<CacheValue(CacheKey)>  loadData;
	const std::function<void(CacheKey,CacheValue)>  saveData;

};


#endif /* DIRECTMAPPEDMULTITHREADCACHE_H_ */
