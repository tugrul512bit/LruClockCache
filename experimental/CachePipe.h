/*
 * CachePipe.h
 *
 *  Created on: Oct 14, 2021
 *      Author: tugrul
 */

#ifndef CACHEPIPE_H_
#define CACHEPIPE_H_

#include <iostream>


#include<vector>
#include<atomic>

template<typename CacheKey, typename CacheValue>
class CacheCommand
{
  public:
  const static char COMMAND_GET=1;
  const static char COMMAND_SET=2;
  const static char COMMAND_FLUSH=3;
  const static char COMMAND_TERMINATE=4;

  CacheCommand()
  {
	  key=CacheKey();
	  value=nullptr;
	  command=0; // no operation
	  containsCommand.store(0);
	  complete=nullptr;
  }

  CacheCommand(CacheKey keyPrm, CacheValue & valuePrm,const char commandPrm, std::atomic<bool> * completePrm)
  {
	  key=keyPrm;
	  value=*valuePrm;
	  command=commandPrm;
	  containsCommand.store(0);
	  complete=completePrm;
  }

  CacheKey key;
  CacheValue * value; // also for the value returned by consumer
  char command; // 1=get, 2=set, 3=flush
  unsigned char padding1[sizeof(std::atomic<bool>) < 64 ? 64 - sizeof(std::atomic<bool>) : 64 ];
  std::atomic<bool> containsCommand;
  unsigned char padding2[sizeof(std::atomic<bool>) < 64 ? 64 - sizeof(std::atomic<bool>) : 64 ];
  std::atomic<bool> * complete;

};

template<typename CacheKey, typename CacheValue>
class CacheMsg
{
  public:
  const static char COMMAND_GET=1;
  const static char COMMAND_SET=2;
  const static char COMMAND_FLUSH=3;

  CacheMsg()
  {
	  key=CacheKey();
	  value=nullptr;
	  command=0; // no operation
	  complete=nullptr;
  }

  CacheMsg(CacheKey keyPrm, CacheValue & valuePrm,const char commandPrm,std::atomic<bool> * completePrm=nullptr)
  {
	  key=keyPrm;
	  value=&valuePrm;
	  command=commandPrm;
	  complete=completePrm;
  }

  CacheMsg(CacheKey keyPrm, CacheValue * valuePrm,const char commandPrm,std::atomic<bool> * completePrm=nullptr)
  {
	  key=keyPrm;
	  value=valuePrm;
	  command=commandPrm;
	  complete=completePrm;
  }

  CacheKey key;
  CacheValue * value; // also for the value returned by consumer
  std::atomic<bool> * complete; // "completed" indicator for client
  char command; // 1=get, 2=set, 3=flush
};



template<typename CacheKey, typename CacheValue>
class CachePipe
{
  public:
  CachePipe()
  {

		head.store(0);
		tail=0;
		cmdQ=std::vector<CacheCommand<CacheKey,CacheValue>>(65536);
  }

  inline
  void producerPush(CacheKey key, CacheValue & value,const char cmdType)
  {
		static thread_local std::atomic<bool> complete(false);
		complete.store(false);

		uint16_t cur=head.fetch_add(1);
		bool bus=cmdQ[cur].containsCommand.load(std::memory_order::acquire);
		while(bus)
		{
		  std::this_thread::yield();
		  bus=cmdQ[cur].containsCommand.load(std::memory_order::acquire);
		}
		cmdQ[cur].command=cmdType;
		cmdQ[cur].key=key;
		cmdQ[cur].value=&value;
		cmdQ[cur].complete = &complete;
		cmdQ[cur].containsCommand.store(true,std::memory_order::release);

		bool completed = cmdQ[cur].complete->load();
		while(!completed)
		{
			std::this_thread::yield();
			completed = cmdQ[cur].complete->load();
		}
  }

  inline
  void consumerTest(bool & success)
  {
		uint16_t cur=tail++;
		bool bus=cmdQ[cur].containsCommand.load(std::memory_order::acquire);

		if(!bus)
		{
			tail--;
			success = false;
		}
		else
		{
			success=true;
		}
  }

  inline
  CacheMsg<CacheKey,CacheValue> consumerTryPop()
  {
		uint16_t cur=tail-1;
		static thread_local CacheMsg<CacheKey,CacheValue> cmd(cmdQ[cur].key,cmdQ[cur].value,cmdQ[cur].command,cmdQ[cur].complete);
		cmd.command=cmdQ[cur].command;
		cmd.complete=cmdQ[cur].complete;
		cmd.key=cmdQ[cur].key;
		cmd.value=cmdQ[cur].value;
		cmdQ[cur].containsCommand.store(false,std::memory_order::release);
		return cmd;
  }

  inline
  CacheMsg<CacheKey,CacheValue> consumerPop()
  {
		uint16_t cur=tail++;
		bool bus=cmdQ[cur].containsCommand.load(std::memory_order::acquire);
		while(!bus)
		{
		  std::this_thread::yield();
		  bus=cmdQ[cur].containsCommand.load(std::memory_order::acquire);
		}

		static thread_local CacheMsg<CacheKey,CacheValue> cmd(cmdQ[cur].key,cmdQ[cur].value,cmdQ[cur].command,cmdQ[cur].complete);
		cmd.command=cmdQ[cur].command;
		cmd.complete=cmdQ[cur].complete;
		cmd.key=cmdQ[cur].key;
		cmd.value=cmdQ[cur].value;
		cmdQ[cur].containsCommand.store(false,std::memory_order::release);
		return cmd;
  }

  private:
  std::vector<CacheCommand<CacheKey,CacheValue>> cmdQ;
  unsigned char padding1[sizeof(std::atomic<uint16_t>) < 64 ? 64 - sizeof(std::atomic<uint16_t>) : 64 ];
  std::atomic<uint16_t> head;
  unsigned char padding2[sizeof(std::atomic<uint16_t>) < 64 ? 64 - sizeof(std::atomic<uint16_t>) : 64 ];
  uint16_t tail;
};





#endif /* CACHEPIPE_H_ */
