/*
 * AtomicCacheController.h
 *
 *  Created on: Oct 14, 2021
 *      Author: root
 */

#ifndef ATOMICCACHECONTROLLER_H_
#define ATOMICCACHECONTROLLER_H_

#include"../integer_key_specialization/DirectMappedCache.h"
#include"../LruClockCache.h"
#include"CachePipe.h"

#include<thread>
#include<functional>
static int pipeId;
template<typename CacheKey, typename CacheValue, typename CacheInternalIntegerType=size_t>
class AtomicCacheController
{
public:
	static int generatePipeId(){ return pipeId++; }
	AtomicCacheController(CacheInternalIntegerType cacheSizeL1, CacheInternalIntegerType cacheSizeL2,
			const std::function<CacheValue(CacheKey)> & cacheMissRead,
			const std::function<void(CacheKey,CacheValue)> & cacheMissWrite
			):lru(cacheSizeL2,cacheMissRead,cacheMissWrite),directMapped(cacheSizeL1,
					[this](CacheKey key){ return this->lru.get(key);},
					[this](CacheKey key, CacheValue value){ this->lru.set(key,value); }), pipe(std::thread::hardware_concurrency()),numThr(std::thread::hardware_concurrency())
	{

		consumer = std::thread([&](){
			bool brk = false;
			while(true)
			{
				if(brk)
					break;
				bool success[numThr];
				for(int i=0;i<numThr;i++)
				{

					pipe[i].consumerTest(success[i]);
				}
				for(int i=0;i<numThr;i++)
				{
					if(success[i])
					{
						auto msg = pipe[i].consumerTryPop();
						if(msg.command == CacheCommand<CacheKey,CacheValue>::COMMAND_GET)
						{
							*msg.value = directMapped.get(msg.key);
						}
						else if (msg.command == CacheCommand<CacheKey,CacheValue>::COMMAND_SET)
						{
							directMapped.set(msg.key,*msg.value);
						}
						else if (msg.command == CacheCommand<CacheKey,CacheValue>::COMMAND_FLUSH)
						{
							directMapped.flush();
							lru.flush();
						}
						else // terminate
						{
							directMapped.flush();
							lru.flush();
							msg.complete->store(1);
							brk=true;
							break;
						}

						msg.complete->store(1);

					}

				}
			}
		});
	}

	inline
	void set(CacheKey key, CacheValue value) noexcept
	{
		CacheValue tmp = value;
		static thread_local int pipeId = generatePipeId();
		pipe[pipeId%numThr].producerPush(key, tmp,CacheCommand<CacheKey,CacheValue>::COMMAND_SET);
	}

	inline
	CacheValue get(CacheKey key) noexcept
	{
		static thread_local CacheValue result;
		static thread_local int pipeId = generatePipeId();
		pipe[pipeId%numThr].producerPush(key, result,CacheCommand<CacheKey,CacheValue>::COMMAND_GET);
		return result;
	}

	// set is threadsafe already
	inline
	void setThreadSafe(CacheKey key, CacheValue value) noexcept
	{
		set(key,value);
	}

	// get is threadsafe already
	inline
	CacheValue getThreadSafe(CacheKey key) noexcept
	{
		return get(key);
	}

	inline
	void flush() noexcept
	{
		CacheValue tmpValue;
		CacheKey tmpKey;
		pipe[0].producerPush(tmpKey, tmpValue,CacheCommand<CacheKey,CacheValue>::COMMAND_FLUSH);
	}

	~AtomicCacheController()
	{
		CacheValue tmpValue;
		CacheKey tmpKey;
		pipe[0].producerPush(tmpKey, tmpValue,CacheCommand<CacheKey,CacheValue>::COMMAND_TERMINATE);

		if(consumer.joinable()) consumer.join();
	}

private:
	int numThr;
	LruClockCache<CacheKey,CacheValue> lru;
	DirectMappedCache<CacheKey,CacheValue> directMapped;
	std::vector<CachePipe<CacheKey,CacheValue>> pipe;

	std::thread consumer;// single consumer thread for cache-coherence

};



#endif /* ATOMICCACHECONTROLLER_H_ */
