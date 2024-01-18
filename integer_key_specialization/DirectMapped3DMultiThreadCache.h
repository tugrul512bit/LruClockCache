/*
 * DirectMapped3DMultiThreadCache.h
 *
 *  Created on: Oct 24, 2021
 *      Author: tugrul
 */

#ifndef DIRECTMAPPED3DMULTITHREADCACHE_H_
#define DIRECTMAPPED3DMULTITHREADCACHE_H_



#include<vector>
#include<functional>
#include<mutex>


/* 3D Direct-mapped cache implementation with granular locking (per-tag)
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
class DirectMapped3DMultiThreadCache
{
public:
	// allocates buffers for numElementsX x numElementsY x numElementsZ number of cache slots/lanes
	// readMiss: 	cache-miss for read operations. User needs to give this function
	// 				to let the cache automatically get data from backing-store
	//				example: [&](MyClass keyX, MyClass keyY, MyClass keyZ){ return backingStore.get(keyX,keyY,keyZ); }
	//				takes a CacheKey as key, returns CacheValue as value
	// writeMiss: 	cache-miss for write operations. User needs to give this function
	// 				to let the cache automatically set data to backing-store
	//				example: [&](MyClass keyX, MyClass keyY, MyClass keyZ, MyAnotherClass value){ backingStore.set(keyX,keyY,keyZ,value); }
	//				takes a CacheKey as key and CacheValue as value
	// numElementsX: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	// numElementsY: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	// numElementsZ: has to be integer-power of 2 (e.g. 2,4,8,16,...)
	// prepareForMultithreading: by default (true) it allocates an array of structs each with its own mutex to evade false-sharing during getThreadSafe/setThreadSafe calls
	//          with a given "false" value, it does not allocate mutex array and getThreadSafe/setThreadSafe methods become undefined behavior under multithreaded-use
	//          true: allocates at least extra 256 bytes per cache tag
	DirectMapped3DMultiThreadCache(CacheKey numElementsX,CacheKey numElementsY,CacheKey numElementsZ,
				const std::function<CacheValue(CacheKey,CacheKey,CacheKey)> & readMiss,
				const std::function<void(CacheKey,CacheKey,CacheKey,CacheValue)> & writeMiss,
				const bool prepareForMultithreading = true):sizeX(numElementsX),sizeY(numElementsY),sizeZ(numElementsZ),sizeXM1(numElementsX-1),sizeYM1(numElementsY-1),sizeZM1(numElementsZ-1),loadData(readMiss),saveData(writeMiss)
	{
		if(prepareForMultithreading)
			mut = std::vector<MutexWithoutFalseSharing>(numElementsX*numElementsY*numElementsZ);
		// initialize buffers
		valueBuffer.reserve(numElementsX*numElementsY*numElementsZ);
		isEditedBuffer.reserve(numElementsX*numElementsY*numElementsZ);
		keyBuffer.reserve(numElementsX*numElementsY*numElementsZ);
		for(size_t i=0;i<numElementsX;i++)
		{
			for(size_t j=0;j<numElementsY;j++)
			{
				for(size_t k=0;k<numElementsZ;k++)
				{
					valueBuffer.push_back(CacheValue());
					isEditedBuffer.push_back(0);
					CacheKey3D key3D;
					key3D.x=CacheKey()-1;
					key3D.y=CacheKey()-1;
					key3D.z=CacheKey()-1;
					keyBuffer.push_back(key3D);
				}
			}
		}
	}



	// get element from cache, Z-major indexing like 3D arrays of C++
	// if cache doesn't find it in buffers,
	// then cache gets data from backing-store
	// then returns the result to user
	// then cache is available from RAM on next get/set access with same key
	inline
	const CacheValue get(const CacheKey & keyX,const CacheKey & keyY,const CacheKey & keyZ)  noexcept
	{
		return accessDirect(keyX,keyY,keyZ,nullptr);
	}


	// thread-safe but slower version of get()
	inline
	const CacheValue getThreadSafe(const CacheKey & keyX,const CacheKey & keyY,const CacheKey & keyZ)  noexcept
	{
		return accessDirectLocked(keyX,keyY,keyZ,nullptr);
	}

	// set element to cache, Z-major indexing like 3D arrays of C++
	// if cache doesn't find it in buffers,
	// then cache sets data on just cache
	// writing to backing-store only happens when
	// 					another access evicts the cache slot containing this key/value
	//					or when cache is flushed by flush() method
	// then returns the given value back
	// then cache is available from RAM on next get/set access with same key
	inline
	void set(const CacheKey & keyX,const CacheKey & keyY,const CacheKey & keyZ, const CacheValue & val) noexcept
	{
		accessDirect(keyX,keyY,keyZ,&val,1);
	}

	// thread-safe but slower version of set()
	inline
	void setThreadSafe(const CacheKey & keyX,const CacheKey & keyY,const CacheKey & keyZ, const CacheValue & val)  noexcept
	{
		accessDirectLocked(keyX,keyY,keyZ,&val,1);
	}

	// use this before closing the backing-store to store the latest bits of data
	void flush()
	{
		try
		{
			const size_t n = sizeX*sizeY*sizeZ;
			if(mut.size()>0)
			{
				for (size_t i=0;i<n;i++)
				{
					std::lock_guard<std::mutex> lg(mut[i].mut);
					if (isEditedBuffer[i] == 1)
					{
						isEditedBuffer[i]=0;
						auto oldKey = keyBuffer[i];
						auto oldValue = valueBuffer[i];
						saveData(oldKey.x,oldKey.y,oldKey.z,oldValue);
					}
				}
			}
			else
			{
				for (size_t i=0;i<n;i++)
				{
					if (isEditedBuffer[i] == 1)
					{
						isEditedBuffer[i]=0;
						auto oldKey = keyBuffer[i];
						auto oldValue = valueBuffer[i];
						saveData(oldKey.x,oldKey.y,oldKey.z,oldValue);
					}
				}
			}
		}catch(std::exception &ex){ std::cout<<ex.what()<<std::endl; }
	}

	// direct mapped cache element access, locked per item for parallelism
	// opType=0: get
	// opType=1: set
	CacheValue accessDirectLocked(const CacheKey & keyX, const CacheKey & keyY, const CacheKey & keyZ,const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		CacheKey tagX = keyX & sizeXM1;
		CacheKey tagY = keyY & sizeYM1;
		CacheKey tagZ = keyZ & sizeZM1;
		const size_t index = tagX*(size_t)sizeY*(size_t)sizeZ+tagY*(size_t)sizeZ + tagZ;
		std::lock_guard<std::mutex> lg(mut[index].mut); // N parallel locks in-flight = less contention in multi-threading

		// compare keys
		const auto oldKey3D = keyBuffer[index];
		const auto newKey3D = CacheKey3D(keyX,keyY,keyZ);
		if(oldKey3D.x == keyX && oldKey3D.y == keyY && oldKey3D.z == keyZ)
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

				saveData(oldKey3D.x,oldKey3D.y,oldKey3D.z,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(newKey3D.x,newKey3D.y,newKey3D.z);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey3D;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey3D;
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
					const CacheValue && loadedData = loadData(newKey3D.x,newKey3D.y,newKey3D.z);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey3D;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey3D;
					return *value;
				}
			}

		}
	}

	// direct mapped cache element access
	// opType=0: get
	// opType=1: set
	CacheValue accessDirect(const CacheKey & keyX, const CacheKey & keyY, const CacheKey & keyZ, const CacheValue * value, const bool opType = 0)
	{

		// find tag mapped to the key
		CacheKey tagX = keyX & sizeXM1;
		CacheKey tagY = keyY & sizeYM1;
		CacheKey tagZ = keyZ & sizeZM1;
		const size_t index = tagX*(size_t)sizeY*(size_t)sizeZ+tagY*(size_t)sizeZ + tagZ;


		// compare keys
		const auto oldKey3D = keyBuffer[index];
		const auto newKey3D = CacheKey3D(keyX,keyY,keyZ);
		if(oldKey3D.x == keyX && oldKey3D.y == keyY && oldKey3D.z == keyZ)
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

				saveData(oldKey3D.x,oldKey3D.y,oldKey3D.z,oldValue);

				// "get"
				if(opType==0)
				{
					const CacheValue && loadedData = loadData(newKey3D.x,newKey3D.y,newKey3D.z);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey3D;
					return loadedData;
				}
				else /* "set" */
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey3D;
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
					const CacheValue && loadedData = loadData(newKey3D.x,newKey3D.y,newKey3D.z);
					valueBuffer[index]=loadedData;
					keyBuffer[index]=newKey3D;
					return loadedData;
				}
				else // "set"
				{
					valueBuffer[index]=*value;
					keyBuffer[index]=newKey3D;
					return *value;
				}
			}

		}
	}



private:
	struct CacheKey3D
	{
		CacheKey3D():x(CacheKey()-1),y(CacheKey()-1),z(CacheKey()-1) { }
		CacheKey3D(CacheKey xPrm, CacheKey yPrm, CacheKey zPrm):x(xPrm),y(yPrm),z(zPrm) { }
		CacheKey x,y,z;
	};
	struct MutexWithoutFalseSharing
	{
		std::mutex mut;
		char padding[256-sizeof(std::mutex) <= 0 ? 4:256-sizeof(std::mutex)];
	};
	const CacheKey sizeX;
	const CacheKey sizeY;
	const CacheKey sizeZ;
	const CacheKey sizeXM1;
	const CacheKey sizeYM1;
	const CacheKey sizeZM1;

	std::vector<MutexWithoutFalseSharing> mut;
	std::vector<CacheValue> valueBuffer;
	std::vector<unsigned char> isEditedBuffer;
	std::vector<CacheKey3D> keyBuffer;

	const std::function<CacheValue(CacheKey,CacheKey,CacheKey)>  loadData;
	const std::function<void(CacheKey,CacheKey,CacheKey,CacheValue)>  saveData;

};







#endif /* DIRECTMAPPED3DMULTITHREADCACHE_H_ */
