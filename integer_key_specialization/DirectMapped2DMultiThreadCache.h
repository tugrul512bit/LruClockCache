/*
 * DirectMapped2DMultiThreadCache.h
 *
 *  Created on: Oct 23, 2021
 *      Author: tugrul
 */

#ifndef DIRECTMAPPED2DMULTITHREADCACHE_H_
#define DIRECTMAPPED2DMULTITHREADCACHE_H_


#include<vector>
#include<functional>
#include<mutex>


/* 2D Direct-mapped cache implementation with granular locking (per-tag)
 *       Only usable for integer type keys in range [0,maxPositive-1] (if key is int then "-1" not usable, if key is uint16_t then "65535" not usable)
 *       since locking protects only items/keys, also the user should make cache-miss functions thread-safe (i.e. adding a lock-guard)
 *       unless backing-store is thread-safe already (or has multi-thread support already)
 * Intended to be used as LLC(last level cache) for CacheThreader instances with getThreadSafe setThreadSafe methods or single threaded with get/set
 * 															to optimize contentions out in multithreaded read-only scenarios
 * Can be used alone, as a read+write multi-threaded cache using getThreadSafe setThreadSafe methods but cache-hit ratio will not be good
 * CacheKey: type of key (only integers: int, char, size_t, uint16_t, ...)
 * CacheValue: type of value that is bound to key (same as above)
 * InternalKeyTypeInteger: type of tag found after modulo operationa (is important for maximum cache size. unsigned char = 255, unsigned int=1024*1024*1024*4)
 */
template<	typename CacheKey, typename CacheValue, typename InternalKeyTypeInteger=size_t>
class DirectMapped2DMultiThreadCache
{
public:
	// allocates buffers for numElementsX x numElementsY number of cache slots/lanes
	// readMiss: 	cache-miss for read operations. User needs to give this function
	// 				to let the cache automatically get data from backing-store
	//				example: [&](MyClass keyX, MyClass keyY){ return backingStore.get(key); }
	//				takes a CacheKey as key, returns CacheValue as value
	// writeMiss: 	cache-miss for write operations. User needs to give this function
	// 				to let the cache automatically set data to backing-store
	//				example: [&](MyClass keyX, MyClass keyY, MyAnotherClass value){ backingStore.set(key,value); }
	//				takes a CacheKey as key and CacheValue as value
	// numElementsX: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	// numElementsY: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	// prepareForMultithreading: by default (true) it allocates an array of structs each with its own mutex to evade false-sharing during getThreadSafe/setThreadSafe calls
	//          with a given "false" value, it does not allocate mutex array and getThreadSafe/setThreadSafe methods become undefined behavior under multithreaded-use
	//          true: allocates at least extra 64 bytes per cache tag
	DirectMapped2DMultiThreadCache(CacheKey numElementsX,CacheKey numElementsY,
				const std::function<CacheValue(CacheKey,CacheKey)> & readMiss,
				const std::function<void(CacheKey,CacheKey,CacheValue)> & writeMiss,
				const bool prepareForMultithreading = true):sizeX(numElementsX),sizeY(numElementsY),sizeXM1(numElementsX-1),sizeYM1(numElementsY-1),loadData(readMiss),saveData(writeMiss)
	{
		if(prepareForMultithreading)
			mut = std::vector<MutexWithoutFalseSharing>(numElementsX*numElementsY);
		// initialize buffers
		valueBuffer.reserve(numElementsX*numElementsY);
		isEditedBuffer.reserve(numElementsX*numElementsY);
		keyBuffer.reserve(numElementsX*numElementsY);
		for(size_t i=0;i<numElementsX;i++)
		{
			for(size_t j=0;j<numElementsY;j++)
			{
				valueBuffer.push_back(CacheValue());
				isEditedBuffer.push_back(0);
				CacheKey2D key2D;
				key2D.x=CacheKey()-1;
				key2D.y=CacheKey()-1;

				keyBuffer.push_back(key2D);
			}
		}
	}



	// get element from cache, row-major like C++ 2D arrays
	// if cache doesn't find it in buffers,
	// then cache gets data from backing-store
	// then returns the result to user
	// then cache is available from RAM on next get/set access with same key
	inline
	const CacheValue get(const CacheKey & keyX,const CacheKey & keyY)  noexcept
	{
		return accessDirect(keyX,keyY,nullptr);
	}


	// thread-safe but slower version of get()
	inline
	const CacheValue getThreadSafe(const CacheKey & keyX,const CacheKey & keyY)  noexcept
	{
		return accessDirectLocked(keyX,keyY,nullptr);
	}

	// set element to cache, row-major like C++ 2D arrays
	// if cache doesn't find it in buffers,
	// then cache sets data on just cache
	// writing to backing-store only happens when
	// 					another access evicts the cache slot containing this key/value
	//					or when cache is flushed by flush() method
	// then returns the given value back
	// then cache is available from RAM on next get/set access with same key
	inline
	void set(const CacheKey & keyX,const CacheKey & keyY, const CacheValue & val) noexcept
	{
		accessDirect(keyX,keyY,&val,1);
	}

	// thread-safe but slower version of set()
	inline
	void setThreadSafe(const CacheKey & keyX,const CacheKey & keyY, const CacheValue & val)  noexcept
	{
		accessDirectLocked(keyX,keyY,&val,1);
	}

	// use this before closing the backing-store to store the latest bits of data
	void flush()
	{
		try
		{
			const size_t n = sizeX*sizeY;
			for (size_t i=0;i<n;i++)
			{
			  if (isEditedBuffer[i] == 1)
			  {
					isEditedBuffer[i]=0;
					auto oldKey = keyBuffer[i];
					auto oldValue = valueBuffer[i];
					saveData(oldKey.x,oldKey.y,oldValue);
			  }
			}
		}catch(std::exception &ex){ std::cout<<ex.what()<<std::endl; }
	}

	// direct mapped cache element access, locked per item for parallelism
	// opType=0: get
	// opType=1: set
	CacheValue const accessDirectLocked(const CacheKey & keyX, const CacheKey & keyY,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		CacheKey tagX = keyX & sizeXM1;
		CacheKey tagY = keyY & sizeYM1;
		const size_t index = tagX*(size_t)sizeY+tagY;
		std::lock_guard<std::mutex> lg(mut[index].mut); // N parallel locks in-flight = less contention in multi-threading

		// compare keys
		const auto oldKey2D = keyBuffer[index];
		const auto newKey2D = CacheKey2D(keyX,keyY);
		if(oldKey2D.x == keyX && oldKey2D.y == keyY)
		{
			// cache-hit

			// "set"
			if(opType == 1)
			{
				isEditedBuffer[index]=1;
				valueBuffer[index]=*value;
			}

			// cache hit value
			return valueBuffer[index];
		}
		else // cache-miss
		{
			CacheValue oldValue = valueBuffer[index];

			// eviction algorithm start
			if(isEditedBuffer[index] == 1)
			{
				// if it is "get"
				if(opType==0)
				{
					isEditedBuffer[index]=0;
				}

				saveData(oldKey2D.x,oldKey2D.y,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(newKey2D.x,newKey2D.y);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey2D;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey2D;
					return *value;
				}
			}
			else // not edited
			{
				// "set"
				if(opType == 1)
				{
					isEditedBuffer[index]=1;
				}

				// "get"
				if(opType == 0)
				{
					const CacheValue && loadedData = loadData(newKey2D.x,newKey2D.y);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey2D;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey2D;
					return *value;
				}
			}

		}
	}

	// direct mapped cache element access
	// opType=0: get
	// opType=1: set
	CacheValue const accessDirect(const CacheKey & keyX, const CacheKey & keyY,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		CacheKey tagX = keyX & sizeXM1;
		CacheKey tagY = keyY & sizeYM1;

		const size_t index = tagX*(size_t)sizeY+tagY;

		// compare keys
		const auto oldKey2D = keyBuffer[index];
		const auto newKey2D = CacheKey2D(keyX,keyY);
		if(oldKey2D.x == keyX && oldKey2D.y == keyY)
		{
			// cache-hit

			// "set"
			if(opType == 1)
			{
				isEditedBuffer[index]=1;
				valueBuffer[index]=*value;
			}

			// cache hit value
			return valueBuffer[index];
		}
		else // cache-miss
		{
			CacheValue oldValue = valueBuffer[index];

			// eviction algorithm start
			if(isEditedBuffer[index] == 1)
			{
				// if it is "get"
				if(opType==0)
				{
					isEditedBuffer[index]=0;
				}

				saveData(oldKey2D.x,oldKey2D.y,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(newKey2D.x,newKey2D.y);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey2D;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey2D;
					return *value;
				}
			}
			else // not edited
			{
				// "set"
				if(opType == 1)
				{
					isEditedBuffer[index]=1;
				}

				// "get"
				if(opType == 0)
				{
					const CacheValue && loadedData = loadData(newKey2D.x,newKey2D.y);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey2D;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey2D;
					return *value;
				}
			}

		}
	}



private:
	struct CacheKey2D
	{
		CacheKey2D():x(CacheKey()-1),y(CacheKey()-1) { }
		CacheKey2D(CacheKey xPrm, CacheKey yPrm):x(xPrm),y(yPrm) { }
		CacheKey x,y;
	};
	struct MutexWithoutFalseSharing
	{
		std::mutex mut;
		char padding[64-sizeof(std::mutex) <= 0 ? 4:64-sizeof(std::mutex)];
	};
	const CacheKey sizeX;
	const CacheKey sizeY;
	const CacheKey sizeXM1;
	const CacheKey sizeYM1;

	std::vector<MutexWithoutFalseSharing> mut;
	std::vector<CacheValue> valueBuffer;
	std::vector<unsigned char> isEditedBuffer;
	std::vector<CacheKey2D> keyBuffer;

	const std::function<CacheValue(CacheKey,CacheKey)>  loadData;
	const std::function<void(CacheKey,CacheKey,CacheValue)>  saveData;

};




#endif /* DIRECTMAPPED2DMULTITHREADCACHE_H_ */
