/*
 * DirectMappedCache.h
 *
 *  Created on: Oct 8, 2021
 *      Author: root
 */

#ifndef DIRECTMAPPEDCACHE_H_
#define DIRECTMAPPEDCACHE_H_

#include<vector>
#include<functional>
#include<mutex>


/* Direct-mapped cache implementation
 * Only usable for integer type keys in range [0,maxPositive-1]
 *
 * CacheKey: type of key (only integers: int, char, size_t)
 * CacheValue: type of value that is bound to key (same as above)
 */
template<	typename CacheKey, typename CacheValue>
class DirectMappedCache
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
	DirectMappedCache(CacheKey numElements,
				const std::function<CacheValue(CacheKey)> & readMiss,
				const std::function<void(CacheKey,CacheValue)> & writeMiss):size(numElements),sizeM1(numElements-1),loadData(readMiss),saveData(writeMiss)
	{
		// initialize buffers
		for(CacheKey i=0;i<numElements;i++)
		{
			valueBuffer.push_back(CacheValue());
			isEditedBuffer.push_back(0);
			keyBuffer.push_back(CacheKey()-1);// mapping of 0+ allowed
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
		std::lock_guard<std::mutex> lg(mut);
		return accessDirect(key,nullptr);
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
		std::lock_guard<std::mutex> lg(mut);
		accessDirect(key,&val,1);
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

	// direct mapped access
	// opType=0: get
	// opType=1: set
	CacheValue const accessDirect(const CacheKey & key,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		CacheKey tag = key & sizeM1;

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
	std::mutex mut;

	std::vector<CacheValue> valueBuffer;
	std::vector<unsigned char> isEditedBuffer;
	std::vector<CacheKey> keyBuffer;
	const std::function<CacheValue(CacheKey)>  loadData;
	const std::function<void(CacheKey,CacheValue)>  saveData;

};


#endif /* DIRECTMAPPEDCACHE_H_ */
