/*
 * LruClockCache.h
 *
 *  Created on: Oct 4, 2021
 *      Author: tugrul
 */

#ifndef LRUCLOCKCACHE_H_
#define LRUCLOCKCACHE_H_

#include<vector>
#include<algorithm>
#include<unordered_map>
#include<functional>
#include<mutex>
#include<unordered_map>


/* LRU-CLOCK-second-chance implementation
 *
 * LruKey: type of key (std::string, int, char, size_t, objects)
 * LruValue: type of value that is bound to key (same as above)
 * ClockHandInteger: just an optional optimization to reduce memory consumption when cache size is equal to or less than 255,65535,4B-1,...
 */
template<	typename LruKey, typename LruValue,typename ClockHandInteger=size_t>
class LruClockCache
{
public:
	// allocates circular buffers for numElements number of cache slots
	// readMiss: 	cache-miss for read operations. User needs to give this function
	// 				to let the cache automatically get data from backing-store
	//				example: [&](MyClass key){ return redis.get(key); }
	//				takes a LruKey as key, returns LruValue as value
	// writeMiss: 	cache-miss for write operations. User needs to give this function
	// 				to let the cache automatically set data to backing-store
	//				example: [&](MyClass key, MyAnotherClass value){ redis.set(key,value); }
	//				takes a LruKey as key and LruValue as value
	LruClockCache(ClockHandInteger numElements,
				const std::function<LruValue(LruKey)> & readMiss,
				const std::function<void(LruKey,LruValue)> & writeMiss):size(numElements),loadData(readMiss),saveData(writeMiss)
	{
		ctr = 0;
		// 50% phase difference between eviction and second-chance hands of the "second-chance" CLOCK algorithm
		ctrEvict = numElements/2;

		//loadData=readMiss;
		//saveData=writeMiss;

		// initialize circular buffers
		for(ClockHandInteger i=0;i<numElements;i++)
		{
			valueBuffer.push_back(LruValue());
			chanceToSurviveBuffer.push_back(0);
			isEditedBuffer.push_back(0);
			keyBuffer.push_back(LruKey());
		}
		mapping.reserve(numElements);
	}



	// get element from cache
	// if cache doesn't find it in circular buffers,
	// then cache gets data from backing-store
	// then returns the result to user
	// then cache is available from RAM on next get/set access with same key
	inline
	const LruValue get(const LruKey & key)  noexcept
	{
		return accessClock2Hand(key,nullptr);
	}

	// only syntactic difference
	inline
	const std::vector<LruValue> getMultiple(const std::vector<LruKey> & key)  noexcept
	{
		const int n = key.size();
		std::vector<LruValue> result(n);

		for(int i=0;i<n;i++)
		{
			result[i]=accessClock2Hand(key[i],nullptr);
		}
		return result;
	}


	// thread-safe but slower version of get()
	inline
	const LruValue getThreadSafe(const LruKey & key) noexcept
	{
		std::lock_guard<std::mutex> lg(mut);
		return accessClock2Hand(key,nullptr);
	}

	// set element to cache
	// if cache doesn't find it in circular buffers,
	// then cache sets data on just cache
	// writing to backing-store only happens when
	// 					another access evicts the cache slot containing this key/value
	//					or when cache is flushed by flush() method
	// then returns the given value back
	// then cache is available from RAM on next get/set access with same key
	inline
	void set(const LruKey & key, const LruValue & val) noexcept
	{
		accessClock2Hand(key,&val,1);
	}

	// thread-safe but slower version of set()
	inline
	void setThreadSafe(const LruKey & key, const LruValue & val)  noexcept
	{
		std::lock_guard<std::mutex> lg(mut);
		accessClock2Hand(key,&val,1);
	}

	// use this before closing the backing-store to store the latest bits of data
	void flush()
	{
		std::lock_guard<std::mutex> lg(mut);
		for (auto mp = mapping.cbegin(); mp != mapping.cend() /* not hoisted */; /* no increment */)
		{
		  if (isEditedBuffer[mp->second] == 1)
		  {
				isEditedBuffer[mp->second]=0;
				auto oldKey = keyBuffer[mp->second];
				auto oldValue = valueBuffer[mp->second];
				saveData(oldKey,oldValue);
				mapping.erase(mp++);    // or "it = m.erase(it)" since C++11
		  }
		  else
		  {
			  	++mp;
		  }
		}
	}

	// CLOCK algorithm with 2 hand counters (1 for second chance for a cache slot to survive, 1 for eviction of cache slot)
	// opType=0: get
	// opType=1: set
	LruValue const accessClock2Hand(const LruKey & key,const LruValue * value, const bool opType = 0)
	{

		// check if it is a cache-hit (in-cache)
		typename std::unordered_map<LruKey,ClockHandInteger>::iterator it = mapping.find(key);
		if(it!=mapping.end())
		{

			chanceToSurviveBuffer[it->second]=1;
			if(opType == 1)
			{
				isEditedBuffer[it->second]=1;
				valueBuffer[it->second]=*value;
			}
			return valueBuffer[it->second];
		}
		else // could not found key in cache, so searching in circular-buffer starts
		{
			long long ctrFound = -1;
			LruValue oldValue;
			LruKey oldKey;
			while(ctrFound==-1)
			{
				// second-chance hand lowers the "chance" status down if its 1 but slot is saved from eviction
				// 1 more chance to be in a cache-hit until eviction-hand finds this
				if(chanceToSurviveBuffer[ctr]>0)
				{
					chanceToSurviveBuffer[ctr]=0;
				}

				// circular buffer has no bounds
				ctr++;
				if(ctr>=size)
				{
					ctr=0;
				}

				// unlucky slot is selected for eviction by eviction hand
				if(chanceToSurviveBuffer[ctrEvict]==0)
				{
					ctrFound=ctrEvict;
					oldValue = valueBuffer[ctrFound];
					oldKey = keyBuffer[ctrFound];
				}

				// circular buffer has no bounds
				ctrEvict++;
				if(ctrEvict>=size)
				{
					ctrEvict=0;
				}
			}

			// eviction algorithm start
			if(isEditedBuffer[ctrFound] == 1)
			{
				// if it is "get"
				if(opType==0)
				{
					isEditedBuffer[ctrFound]=0;
				}

				saveData(oldKey,oldValue);

				// "get"
				if(opType==0)
				{
					const LruValue && loadedData = loadData(key);
					mapping.erase(keyBuffer[ctrFound]);
					valueBuffer[ctrFound]=loadedData;
					chanceToSurviveBuffer[ctrFound]=0;

					mapping.emplace(key,ctrFound);
					keyBuffer[ctrFound]=key;

					return loadedData;
				}
				else /* "set" */
				{
					mapping.erase(keyBuffer[ctrFound]);


					valueBuffer[ctrFound]=*value;
					chanceToSurviveBuffer[ctrFound]=0;

					mapping.emplace(key,ctrFound);
					keyBuffer[ctrFound]=key;
					return *value;
				}
			}
			else // not edited
			{
				// "set"
				if(opType == 1)
				{
					isEditedBuffer[ctrFound]=1;
				}

				// "get"
				if(opType == 0)
				{
					const LruValue && loadedData = loadData(key);
					mapping.erase(keyBuffer[ctrFound]);
					valueBuffer[ctrFound]=loadedData;
					chanceToSurviveBuffer[ctrFound]=0;

					mapping.emplace(key,ctrFound);
					keyBuffer[ctrFound]=key;

					return loadedData;
				}
				else // "set"
				{
					mapping.erase(keyBuffer[ctrFound]);


					valueBuffer[ctrFound]=*value;
					chanceToSurviveBuffer[ctrFound]=0;

					mapping.emplace(key,ctrFound);
					keyBuffer[ctrFound]=key;
					return *value;
				}
			}

		}
	}


private:
	const ClockHandInteger size;
	std::mutex mut;
	std::unordered_map<LruKey,ClockHandInteger> mapping;
	std::vector<LruValue> valueBuffer;
	std::vector<unsigned char> chanceToSurviveBuffer;
	std::vector<unsigned char> isEditedBuffer;
	std::vector<LruKey> keyBuffer;
	const std::function<LruValue(LruKey)>  loadData;
	const std::function<void(LruKey,LruValue)>  saveData;
	ClockHandInteger ctr;
	ClockHandInteger ctrEvict;
};



#endif /* LRUCLOCKCACHE_H_ */
