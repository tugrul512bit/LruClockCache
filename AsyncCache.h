/*
 * AsyncCache.h
 *
 *  Created on: Oct 28, 2021
 *      Author: tugrul
 *      Idea of vector-swapping for producer-consumer communication: Joe Zbiciak
 */

#ifndef ASYNCCACHE_H_
#define ASYNCCACHE_H_

#include "integer_key_specialization/NWaySetAssociativeMultiThreadCache.h"
#include "integer_key_specialization/DirectMappedMultiThreadCache.h"
#include <vector>
#include <mutex>
#include<thread>
#include<memory>
#include<condition_variable>
#include <chrono>
static int threadSlotId=0;

// another multi-level cache for integer keys but asynchronous to the caller of get/set
// optimized for batch-lookup and thread-safe
template<typename CacheKey, typename CacheValue>
class AsyncCache
{
public:
	// composed of 2 caches,
	//		L1=direct mapped cache which is client of L2,
	//		L2=n-way set-associative LRU approximation which is client of backing-store given inside cache-miss functions
	//	L1tags = number of item slots in L1 cache = power of 2 value required
	//	L2sets = number of LRUs in L2 cache = power of 2 value required
	//	L2tagsPerSet = number of item slots per LRU in n-way set associative
	//		total L2 items = L2sets x L2tagsperset
	//	readCacheMiss = function that is called by cache when a key is not found in cache, to read data from backing-store
	//	writeCacheMiss = function that is called by cache when data is cache is evicted, to write on backing-store
	AsyncCache(const size_t L1tags, const size_t L2sets, const size_t L2tagsPerSet,
			const std::function<CacheValue(CacheKey)> & readCacheMiss,
			const std::function<void(CacheKey,CacheValue)> & writeCacheMiss,
			const int numProducersPrm = 8 /* has to be power of 2 */
	):	numProducers(numProducersPrm),numProducersM1(numProducersPrm-1),
		locks(numProducersPrm),
		L2(L2sets,L2tagsPerSet,readCacheMiss,writeCacheMiss),
		L1(L1tags,[this](CacheKey key){ return this->L2.get(key); },[this](CacheKey key, CacheValue value){ this->L2.set(key,value); }),
		cmdQueue(numProducersPrm),
		cmdQueueForConsumer(numProducersPrm),
		cmdQueuePtr(numProducersPrm),
		cmdQueueForConsumerPtr(numProducersPrm),
		barriers(numProducersPrm)
	{
		for(int i=0;i<numProducers;i++)
		{
			barriers[i]=true;
			cmdQueue[i]=std::make_unique<std::vector<Command>>();
			cmdQueueForConsumer[i]=std::make_unique<std::vector<Command>>();
			cmdQueuePtr[i]=cmdQueue[i].get();
			cmdQueueForConsumerPtr[i]=cmdQueueForConsumer[i].get();
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
						std::lock_guard<std::mutex> lg(locks[i].mut);
						std::swap(cmdQueueForConsumerPtr[i],cmdQueuePtr[i]);
				}

				for(int i=0;i<numProducers;i++)
				{
					std::vector<Command> * const queue = cmdQueueForConsumerPtr[i];
					const std::vector<Command> & queueRef =  *queue;
					const int numWork = queue->size();
					workToDo += numWork;
					for(int j=0;j<numWork;j++)
					{
						switch(queueRef[j].cmd)
						{
							case 0: // get
								*(queueRef[j].valuePtr) = L1.get(queueRef[j].key);
								break;
							case 1: // set
								L1.set(queueRef[j].key,queueRef[j].value);
								break;
							case 2: // flush
								L1.flush();
								L2.flush();
								break;
							case 3: // terminate
								L1.flush();
								L2.flush();
								work=false;
								break;
							default: break;
						}

					}

					if(numWork>0)
						queue->clear(); // no deallocation


					if(!work || numWork==0)
					{
						std::lock_guard<std::mutex> lg(locks[i].mut);
						barriers[i]=true;

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
		std::lock_guard<std::mutex> lg(locks[slotMod].mut);
		cmdQueuePtr[slotMod]->emplace_back(Command(key,valPtr,CacheValue(),0)); // no reallocation after some time
		return slot;
	}

	// asynchronously set the value by key, return slot id for current operation
	int setAsync(const CacheKey & key, const CacheValue & val, const int slotOptional=-1)
	{
		static thread_local int slotStatic = generateThreadSlotId();
		const int slot = (slotOptional==-1 ? slotStatic : slotOptional);
		const int slotMod = slot&numProducersM1;
		std::lock_guard<std::mutex> lg(locks[slotMod].mut);
		cmdQueuePtr[slotMod]->emplace_back(Command(key,nullptr,val,1));
		return slot;
	}

	// asynchronously flush cache, slot number not important
	void flush()
	{
		for(int i=0;i<numProducers;i++)
		{
			std::lock_guard<std::mutex> lg(locks[i].mut);
			cmdQueuePtr[i]->push_back(Command(2));
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
					std::lock_guard<std::mutex> lg(locks[i].mut);
					barriers[i]=false;
				}
				bool wait = true;
				while(wait)
				{
					std::this_thread::yield();
					std::lock_guard<std::mutex> lg(locks[i].mut);
					wait = !barriers[i];
				}
			}
		}
		else
		{
			const int slotMod = slot&numProducersM1;
			{
				std::lock_guard<std::mutex> lg(locks[slotMod].mut);
				barriers[slotMod]=false;
			}
			bool wait = true;
			while(wait)
			{
				std::this_thread::yield();
				std::lock_guard<std::mutex> lg(locks[slotMod].mut);
				wait = !barriers[slotMod];
			}
		}
	}

	~AsyncCache()
	{
		barrier();

		{
			std::lock_guard<std::mutex> lg(locks[0].mut);
			cmdQueuePtr[0]->push_back(Command(3));
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
		std::mutex mut;
		char padding[64-sizeof(std::mutex) <= 0 ? 4:64-sizeof(std::mutex)];
	};
	struct Command
	{
		const CacheKey key;
		const CacheValue value;
		CacheValue * const valuePtr;
		const char cmd; // 0=get, 1=set, 2=flush, 3=terminate

		Command():key(),value(),valuePtr(nullptr),cmd(-1){ }
		Command(char cmdPrm):key(),value(),valuePtr(nullptr),cmd(cmdPrm){}
		Command (CacheKey keyPrm, CacheValue * ptr,CacheValue val, char cmdPrm):key(keyPrm),value(val),valuePtr(ptr),cmd(cmdPrm)
		{

		}

	};
	std::vector<MutexWithoutFalseSharing> locks;
	NWaySetAssociativeMultiThreadCache<CacheKey,CacheValue> L2;
	DirectMappedMultiThreadCache<CacheKey,CacheValue> L1;
	std::vector<std::unique_ptr<std::vector<Command>>> cmdQueue;
	std::vector<std::unique_ptr<std::vector<Command>>> cmdQueueForConsumer;
	std::vector<std::vector<Command> *> cmdQueuePtr;
	std::vector<std::vector<Command> *> cmdQueueForConsumerPtr;
	std::thread consumer;
	std::vector<bool> barriers;
	//std::condition_variable signal;
};



#endif /* ASYNCCACHE_H_ */
