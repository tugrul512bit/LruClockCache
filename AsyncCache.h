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
static int threadSlotId=0;

template<typename CacheKey, typename CacheValue>
class AsyncCache
{
public:
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
			while(work)
			{

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
		cmdQueuePtr[slotMod]->push_back(Command(key,valPtr,CacheValue(),0)); // no reallocation after some time
		return slot;
	}

	// asynchronously set the value by key, return slot id for current operation
	int setAsync(const CacheKey & key, const CacheValue & val, const int slotOptional=-1)
	{
		static thread_local int slotStatic = generateThreadSlotId();
		const int slot = (slotOptional==-1 ? slotStatic : slotOptional);
		const int slotMod = slot&numProducersM1;
		std::lock_guard<std::mutex> lg(locks[slotMod].mut);
		cmdQueuePtr[slotMod]->push_back(Command(key,nullptr,val,1));
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
		CacheKey key;
		CacheValue value;
		CacheValue * valuePtr;
		char cmd; // 0=get, 1=set, 2=flush, 3=terminate

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
};



#endif /* ASYNCCACHE_H_ */