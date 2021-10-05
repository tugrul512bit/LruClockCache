/*
 * LruClockCacheLine.h
 *
 *  Created on: Oct 5, 2021
 *      Author: root
 */

#ifndef LRUCLOCKCACHEDARRAY_H_
#define LRUCLOCKCACHEDARRAY_H_

#include"../LruClockCache.h"
#include<memory>
#include<map>

// only integer keys are allowed and unbounded access is possible such as arr.get(10000000000000ul)
// as long as the big key does not cause any problem on cache-miss functions given by user
template<typename LruValue, int CacheLineWidth=4>
class CachedArray
{
public:
	CachedArray	(	/* backing-store capacity = unlimited, cache capacity = up to RAM size*/
					size_t cacheCapacity,

					/* backing-store "get" operation that runs when a cache-miss happens */
					const std::function<std::vector<LruValue>(size_t)> & readCacheMiss,

					/* backing-store "set" operation that runs when a cache-miss happens */
					const std::function<void(size_t,std::vector<LruValue>)> & writeCacheMiss
				)
	{
		cachePtr = std::unique_ptr<LruClockCache<size_t,std::vector<LruValue>>>(
				new LruClockCache <size_t,std::vector<LruValue>>(cacheCapacity/CacheLineWidth, readCacheMiss, writeCacheMiss));
		cachePtr->populateCacheLines(CacheLineWidth,LruValue());
	}

	LruValue get(const size_t key) const
	{
		LruValue tmp;
		return cachePtr->getLane(key/CacheLineWidth, tmp, (int)( key%CacheLineWidth));
	}


	void set(const size_t & key, const LruValue & value) const
	{
		auto line = key/CacheLineWidth;
		auto lane = key%CacheLineWidth;
		cachePtr->setLane(line,value,lane);
	}

	std::vector<LruValue> getSubArray(const size_t & key, const int & range) const
	{
		std::vector<LruValue> tmp;
		std::vector<LruValue> result;

		const size_t chunk1start = key/CacheLineWidth;
		const int chunk1startlane = key%CacheLineWidth;

		const size_t chunk1range = (CacheLineWidth - chunk1startlane > range ? range:CacheLineWidth - chunk1startlane );

		size_t chunk2start = chunk1start+1;
		const int chunk2startlane = 0;

		size_t nRepeat = (range - chunk1range)/CacheLineWidth;

		size_t chunk3start = chunk1start+1+nRepeat;
		const int chunk3startlane = 0;
		size_t chunk3range = (key+range)%CacheLineWidth;


		auto chunk1 = cachePtr->getLanes(chunk1start, tmp, chunk1startlane,chunk1range);
		//std::cout<<"chunk1start: "<<chunk1start<<" chunk1.size="<<chunk1.size()<<" chunk1startlane="<<chunk1startlane<<" chunk1range="<<chunk1range<<std::endl;
		result.insert(result.end(),chunk1.begin(),chunk1.end());

		if(nRepeat>0)
		{
			for(size_t i=0;i<nRepeat;i++)
			{
				auto chunk2 = cachePtr->getLanes(chunk2start+i, tmp, chunk2startlane,CacheLineWidth);
				//std::cout<<"chunk2start+i: "<<chunk2start+i<<" chunk2.size="<<chunk2.size()<<" chunk2startlane="<<chunk2startlane<<" chunk2range="<<CacheLineWidth<<std::endl;
				result.insert(result.end(),chunk2.begin(),chunk2.end());
			}
		}

		if(chunk3range>0 && range>chunk1range)
		{
			auto chunk3 = cachePtr->getLanes(chunk3start, tmp, chunk3startlane,chunk3range);
			//std::cout<<"chunk3start: "<<chunk3start<<" chunk3.size="<<chunk3.size()<<" chunk3startlane="<<chunk3startlane<<" chunk3range="<<chunk3range<<std::endl;
			result.insert(result.end(),chunk3.begin(),chunk3.end());
		}

		return result;
	}


	void setSubArray(const std::vector<LruValue> & arr, const size_t & key, const size_t & range) const
	{


		const size_t chunk1start = key/CacheLineWidth;
		const int chunk1startlane = key%CacheLineWidth;

		const size_t chunk1range = (CacheLineWidth - chunk1startlane > range ? range:CacheLineWidth - chunk1startlane );

		size_t chunk2start = chunk1start+1;
		const int chunk2startlane = 0;

		size_t nRepeat = (range - chunk1range)/CacheLineWidth;

		size_t chunk3start = chunk1start+1+nRepeat;
		const int chunk3startlane = 0;
		size_t chunk3range = (key+range)%CacheLineWidth;
		auto chunk1 = std::vector<LruValue>(arr.cbegin(), arr.cbegin()+chunk1range);
		//std::cout<<"chunk1start: "<<chunk1start<<" chunk1.size="<<chunk1.size()<<" chunk1startlane="<<chunk1startlane<<" chunk1range="<<chunk1range<<std::endl;
		cachePtr->setLanes(chunk1start, chunk1, chunk1startlane,chunk1range);


		if(nRepeat>0)
		{
			for(size_t i=0;i<nRepeat;i++)
			{
				auto chunk2 = std::vector<LruValue>(arr.cbegin()+chunk1range+i*CacheLineWidth, arr.cbegin()+chunk1range+i*CacheLineWidth+CacheLineWidth);
				//std::cout<<"chunk2start+i: "<<chunk2start+i<<" chunk2.size="<<chunk2.size()<<" chunk2startlane="<<chunk2startlane<<" chunk2range="<<CacheLineWidth<<std::endl;
				cachePtr->setLanes(chunk2start+i, chunk2, chunk2startlane,CacheLineWidth);
			}
		}

		if(chunk3range>0 && range>chunk1range)
		{
			auto chunk3 = std::vector<LruValue>(arr.cbegin()+chunk1range+nRepeat*CacheLineWidth, arr.cbegin()+chunk1range+nRepeat*CacheLineWidth+chunk3range);
			//std::cout<<"chunk3start: "<<chunk3start<<" chunk3.size="<<chunk3.size()<<" chunk3startlane="<<chunk3startlane<<" chunk3range="<<chunk3range<<std::endl;
			cachePtr->setLanes(chunk3start, chunk3, chunk3startlane,chunk3range);
		}


	}

	// set multiple values with multiple keys


	void flush(){ cachePtr->flush(); }
private:
	std::unique_ptr<LruClockCache<size_t,std::vector<LruValue>>> cachePtr;
};

#endif /* LRUCLOCKCACHEDARRAY_H_ */
