/*
 * AsyncCache.h
 *
 *  Created on: Oct 28, 2021
 *      Author: tugrul
 *      Idea of vector-swapping for producer-consumer communication: Joe Zbiciak
 */

#ifndef ASYNCCACHE_H_
#define ASYNCCACHE_H_

#include "LruClockCache.h"
#include "integer_key_specialization/DirectMappedCache.h"
#include "integer_key_specialization/DirectMappedCacheShard.h"
#include <vector>
#include <mutex>
#include<thread>
#include<memory>
#include<condition_variable>
#include <chrono>
static int threadSlotId=0;

// another multi-level cache for integer keys but asynchronous to the caller of get/set
// optimized for batch-lookup and thread-safe
template<typename CacheKey, typename CacheValue, typename DirectMappedType=DirectMappedCache<CacheKey,CacheValue>>
class AsyncCache
{
public:
	// composed of 2 caches,
	//		L1=direct mapped cache which is client of L2,
	//		L2=LRU approximation which is client of backing-store given inside cache-miss functions
	//	L1tags = number of item slots in L1 cache = power of 2 value required
	//	L2tags = number of item slots in L1 cache
	//	readCacheMiss = function that is called by cache when a key is not found in cache, to read data from backing-store
	//	writeCacheMiss = function that is called by cache when data is cache is evicted, to write on backing-store
	AsyncCache(const size_t L1tags, const size_t L2tags,
			const std::function<CacheValue(CacheKey)> & readCacheMiss,
			const std::function<void(CacheKey,CacheValue)> & writeCacheMiss,
			const int numProducersPrm = 8, /* has to be power of 2 */
			const int zenithShards=0, /* unused for AsyncCache alone */
			const int zenithLane=0 /* unused for AsyncCache alone */
	):	numProducers(numProducersPrm),numProducersM1(numProducersPrm-1),
		locks(numProducersPrm),
		L2(L2tags,readCacheMiss,writeCacheMiss),
		L1(L1tags,
				[this](CacheKey key){ return this->L2.get(key); },
				[this](CacheKey key, CacheValue value){ this->L2.set(key,value); },
				zenithShards,zenithLane
		),

		cmdQueueGet(numProducersPrm),
		cmdQueueForConsumerGet(numProducersPrm),
		cmdQueuePtrGet(numProducersPrm),
		cmdQueueForConsumerPtrGet(numProducersPrm),

		cmdQueueSet(numProducersPrm),
		cmdQueueForConsumerSet(numProducersPrm),
		cmdQueuePtrSet(numProducersPrm),
		cmdQueueForConsumerPtrSet(numProducersPrm),

		cmdQueueFlush(numProducersPrm),
		cmdQueueForConsumerFlush(numProducersPrm),
		cmdQueuePtrFlush(numProducersPrm),
		cmdQueueForConsumerPtrFlush(numProducersPrm),

		cmdQueueTerminate(numProducersPrm),
		cmdQueueForConsumerTerminate(numProducersPrm),
		cmdQueuePtrTerminate(numProducersPrm),
		cmdQueueForConsumerPtrTerminate(numProducersPrm),

		barriers(numProducersPrm)
	{

		for(int i=0;i<numProducers;i++)
		{
			barriers[i].bar=true;
			cmdQueueGet[i]=std::make_unique<std::vector<CommandGet>>();
			cmdQueueForConsumerGet[i]=std::make_unique<std::vector<CommandGet>>();
			cmdQueuePtrGet[i]=cmdQueueGet[i].get();
			cmdQueueForConsumerPtrGet[i]=cmdQueueForConsumerGet[i].get();

			cmdQueueSet[i]=std::make_unique<std::vector<CommandSet>>();
			cmdQueueForConsumerSet[i]=std::make_unique<std::vector<CommandSet>>();
			cmdQueuePtrSet[i]=cmdQueueSet[i].get();
			cmdQueueForConsumerPtrSet[i]=cmdQueueForConsumerSet[i].get();

			cmdQueueFlush[i]=std::make_unique<std::vector<CommandFlush>>();
			cmdQueueForConsumerFlush[i]=std::make_unique<std::vector<CommandFlush>>();
			cmdQueuePtrFlush[i]=cmdQueueFlush[i].get();
			cmdQueueForConsumerPtrFlush[i]=cmdQueueForConsumerFlush[i].get();

			cmdQueueTerminate[i]=std::make_unique<std::vector<CommandTerminate>>();
			cmdQueueForConsumerTerminate[i]=std::make_unique<std::vector<CommandTerminate>>();
			cmdQueuePtrTerminate[i]=cmdQueueTerminate[i].get();
			cmdQueueForConsumerPtrTerminate[i]=cmdQueueForConsumerTerminate[i].get();
		}
		consumer=std::thread([&](){

			bool work = true;
			unsigned int workToDo = 0;
			unsigned int idleCycle = 0;
			while(work)
			{

				workToDo = 0;
				for(int i=0;i<numProducers;i++)
				{
						//std::lock_guard<std::mutex> lg(locks[i].mut);
						locks.lock(i);
						std::swap(cmdQueueForConsumerPtrGet[i],cmdQueuePtrGet[i]);
						std::swap(cmdQueueForConsumerPtrSet[i],cmdQueuePtrSet[i]);
						std::swap(cmdQueueForConsumerPtrFlush[i],cmdQueuePtrFlush[i]);
						std::swap(cmdQueueForConsumerPtrTerminate[i],cmdQueuePtrTerminate[i]);
						locks.unlock(i);
				}

				for(int i=0;i<numProducers;i++)
				{
					int numWorkGet = 0;
					int numWorkSet = 0;
					int numWorkFlush = 0;
					int numWorkTerminate = 0;
					{
						std::vector<CommandGet> * const queue = cmdQueueForConsumerPtrGet[i];
						const std::vector<CommandGet> & queueRef =  *queue;
						const int numWork = queue->size();
						numWorkGet = numWork;
						workToDo += numWork;
						for(int j=0;j<numWork;j++)
						{
							*(queueRef[j].valuePtr) = L1.get(queueRef[j].key);
						}

						if(numWork>0)
							queue->clear(); // no deallocation
					}

					{
						std::vector<CommandSet> * const queue = cmdQueueForConsumerPtrSet[i];
						const std::vector<CommandSet> & queueRef =  *queue;
						const int numWork = queue->size();
						numWorkSet = numWork;
						workToDo += numWork;
						for(int j=0;j<numWork;j++)
						{
							L1.set(queueRef[j].key,queueRef[j].value);
						}

						if(numWork>0)
							queue->clear(); // no deallocation
					}

					{
						std::vector<CommandFlush> * const queue = cmdQueueForConsumerPtrFlush[i];
						const std::vector<CommandFlush> & queueRef =  *queue;
						const int numWork = queue->size();
						numWorkFlush = numWork;
						workToDo += numWork;
						for(int j=0;j<numWork;j++)
						{
							L1.flush();
							L2.flush();
						}

						if(numWork>0)
							queue->clear(); // no deallocation
					}

					{
						std::vector<CommandTerminate> * const queue = cmdQueueForConsumerPtrTerminate[i];
						const std::vector<CommandTerminate> & queueRef =  *queue;
						const int numWork = queue->size();
						numWorkTerminate = numWork;
						workToDo += numWork;
						for(int j=0;j<numWork;j++)
						{
							L1.flush();
							L2.flush();
							work=false;
							break;
						}

						if(numWork>0)
							queue->clear(); // no deallocation
					}

					if(!work || (numWorkGet==0 && numWorkSet==0 && numWorkFlush==0 && numWorkTerminate==0))
					{
						//std::lock_guard<std::mutex> lg(locks[i].mut);
						locks.lock(i);
						barriers[i].bar=true;
						locks.unlock(i);
					}
				}

				if(workToDo == 0)
				{
					idleCycle++;
					if(idleCycle>=100)
					{
						idleCycle=0;
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
				}
			}

		});

	}

	// asynchronously get the value by key, return slot id for current operation
	int getAsync(const CacheKey & key, CacheValue * valPtr, const int slotOptional=-1)
	{
		static thread_local int slotStatic = generateThreadSlotId();
		const int slot = (slotOptional==-1 ? slotStatic : slotOptional);
		const int slotMod = slot&numProducersM1;
		//std::lock_guard<std::mutex> lg(locks[slotMod].mut);
		locks.lock(slotMod);
		cmdQueuePtrGet[slotMod]->emplace_back(CommandGet(key,valPtr)); // no reallocation after some time
		locks.unlock(slotMod);
		return slot;
	}

	// asynchronously set the value by key, return slot id for current operation
	int setAsync(const CacheKey & key, const CacheValue & val, const int slotOptional=-1)
	{
		static thread_local int slotStatic = generateThreadSlotId();
		const int slot = (slotOptional==-1 ? slotStatic : slotOptional);
		const int slotMod = slot&numProducersM1;
		//std::lock_guard<std::mutex> lg(locks[slotMod].mut);
		locks.lock(slotMod);
		cmdQueuePtrSet[slotMod]->emplace_back(CommandSet(key,val));
		locks.unlock(slotMod);
		return slot;
	}

	// asynchronously flush cache, slot number not important
	void flush()
	{
		for(int i=0;i<numProducers;i++)
		{
			//std::lock_guard<std::mutex> lg(locks[i].mut);
			locks.lock(i);
			cmdQueuePtrFlush[i]->push_back(CommandFlush());
			locks.unlock(i);
		}
		for(int i=0;i<numProducers;i++)
			barrier(i);
	}

	// wait for read/write operations on a slot to complete
	void barrier(const int slot=-1)
	{
		if(slot == -1)
		{
			for(int i=0;i<numProducers;i++)
			{
				{
					//std::lock_guard<std::mutex> lg(locks[i].mut);
					locks.lock(i);
					barriers[i].bar=false;
					locks.unlock(i);
				}
				bool wait = true;
				while(wait)
				{
					std::this_thread::yield();
					//std::lock_guard<std::mutex> lg(locks[i].mut);
					locks.lock(i);
					wait = !barriers[i].bar;
					locks.unlock(i);
				}
			}
		}
		else
		{
			const int slotMod = slot&numProducersM1;
			{
				//std::lock_guard<std::mutex> lg(locks[slotMod].mut);
				locks.lock(slotMod);
				barriers[slotMod].bar=false;
				locks.unlock(slotMod);
			}
			bool wait = true;
			while(wait)
			{
				std::this_thread::yield();
				//std::lock_guard<std::mutex> lg(locks[slotMod].mut);
				locks.lock(slotMod);
				wait = !barriers[slotMod].bar;
				locks.unlock(slotMod);
			}
		}
	}

	~AsyncCache()
	{
		barrier();

		{
			//std::lock_guard<std::mutex> lg(locks[0].mut);
			locks.lock(0);
			cmdQueuePtrTerminate[0]->push_back(CommandTerminate());
			locks.unlock(0);
		}


		if(consumer.joinable())
			consumer.join();
	}

private:
	static int generateThreadSlotId(){ return threadSlotId++; }
	const int numProducers;
	const int numProducersM1;
	struct MutexWithoutFalseSharing
	{
		char paddingEdge[256];
		std::mutex mut;
		char padding[256-sizeof(std::mutex) <= 0 ? 4:256-sizeof(std::mutex)];
	};
	struct BarrierWithoutFalseSharing
	{
		char paddingEdge[256];
		bool bar;
		char padding[256-sizeof(bool) <= 0 ? 4:256-sizeof(bool)];
	};
	struct CommandGet
	{
		const CacheKey key;
		CacheValue * const valuePtr;

		CommandGet():key(),valuePtr(nullptr){ }
		CommandGet(char cmdPrm):key(),valuePtr(nullptr){}
		CommandGet(CacheKey keyPrm, CacheValue * ptr):key(keyPrm),valuePtr(ptr)
		{

		}

	};

	struct CommandSet
	{
		const CacheKey key;
		const CacheValue value;

		CommandSet():key(),value(){ }
		CommandSet(char cmdPrm):key(),value(){}
		CommandSet(CacheKey keyPrm,CacheValue val):key(keyPrm),value(val)
		{

		}

	};

	struct CommandFlush
	{
		const char cmd;
		CommandFlush():cmd(){ }
	};

	struct CommandTerminate
	{
		const char cmd;
		CommandTerminate():cmd(){ }
	};



	class AtomicWithoutFalseSharing{
	public:
		char paddingEdge[256];
		std::atomic<bool> flag;
		char padding[256>sizeof(std::atomic<bool>) ? 256-sizeof(std::atomic<bool>):4];
	};

	class FastMutex {

	    std::vector<AtomicWithoutFalseSharing> flag;

	public:
	    FastMutex(){}
	    FastMutex(int n):flag(n){}
	    void lock(int i)
	    {
	        while (flag[i].flag.exchange(true, std::memory_order_relaxed));
	        std::atomic_thread_fence(std::memory_order_acquire);
	    }

	    void unlock(int i)
	    {
	        std::atomic_thread_fence(std::memory_order_release);
	        flag[i].flag.store(false, std::memory_order_relaxed);
	    }
	};

	//std::vector<MutexWithoutFalseSharing> locks;
	FastMutex locks;
	LruClockCache<CacheKey,CacheValue> L2;
	DirectMappedType L1;

	std::vector<std::unique_ptr<std::vector<CommandGet>>> cmdQueueGet;
	std::vector<std::unique_ptr<std::vector<CommandGet>>> cmdQueueForConsumerGet;
	std::vector<std::vector<CommandGet> *> cmdQueuePtrGet;
	std::vector<std::vector<CommandGet> *> cmdQueueForConsumerPtrGet;

	std::vector<std::unique_ptr<std::vector<CommandSet>>> cmdQueueSet;
	std::vector<std::unique_ptr<std::vector<CommandSet>>> cmdQueueForConsumerSet;
	std::vector<std::vector<CommandSet> *> cmdQueuePtrSet;
	std::vector<std::vector<CommandSet> *> cmdQueueForConsumerPtrSet;

	std::vector<std::unique_ptr<std::vector<CommandFlush>>> cmdQueueFlush;
	std::vector<std::unique_ptr<std::vector<CommandFlush>>> cmdQueueForConsumerFlush;
	std::vector<std::vector<CommandFlush> *> cmdQueuePtrFlush;
	std::vector<std::vector<CommandFlush> *> cmdQueueForConsumerPtrFlush;

	std::vector<std::unique_ptr<std::vector<CommandTerminate>>> cmdQueueTerminate;
	std::vector<std::unique_ptr<std::vector<CommandTerminate>>> cmdQueueForConsumerTerminate;
	std::vector<std::vector<CommandTerminate> *> cmdQueuePtrTerminate;
	std::vector<std::vector<CommandTerminate> *> cmdQueueForConsumerPtrTerminate;

	std::thread consumer;
	std::vector<BarrierWithoutFalseSharing> barriers;
	//std::condition_variable signal;
};



#endif /* ASYNCCACHE_H_ */
