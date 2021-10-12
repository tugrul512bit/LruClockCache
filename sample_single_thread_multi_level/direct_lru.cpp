#include<iostream>
#include<vector>
#include"DirectMappedCache.h"
#include"LruClockCache.h"

// up to a billion lookups per second for vectorizable+cache friendly access pattern
// down to 50 million lookups per second for totally random access
int main()
{

  std::vector<char> backingStore(10000);

  L2 is client of backing store
  LruClockCache<size_t,char> L2(1000,
    [&](size_t key){ return backingStore[key];},
    [&](size_t key, char value){ backingStore[key]=value;}
  );

  // L1 is client of L2
  DirectMappedCache<size_t,char> L1(100,
    [&](size_t key){ return L2.get(key);},
    [&](size_t key, char value){ L2.set(key,value);}
  );
 
  // use only L1
  L1.set(9500,120);
  std::cout<<L1.get(9500)<<std::endl;

  // send awaiting bits of data to backing store
  L1.flush();
  L2.flush();

  std::cout<<backingStore[9500]<<std::endl;
  return 0;
}
