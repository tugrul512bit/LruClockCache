#include<vector>
#include<iostream>
#include<memory>
#include "DirectMappedMultiThreadCache.h"
#include "CacheThreader.h"

int main()
{
                // ok to access different indices multithreaded
 		std::vector<int> backingStore(100000);

		auto LLC=std::make_shared<DirectMappedMultiThreadCache<int,int>>(16 /* power of 2 */,
				[&](int key){ return backingStore[key]; },
				[&](int key, int value){ backingStore[key]=value; });

		// optional init from LLC
		for(int i=0;i<20;i++)
			LLC->setThreadSafe(i,i*2);


		#pragma omp parallel for
		for(int i=0;i<8;i++)
		{
			int L2size = 10;
			int L1size = 4 /* power of 2 */;
      // each thread creates its own private cache connected to the common LLC instance
			CacheThreader<DirectMappedMultiThreadCache,int,int> multiLevelCache(LLC,L1size,L2size);
			std::string result;
			for(int j=0;j<20;j++)
			{
				result += "thread-";
				result += std::to_string(i);
				result += ": value for key(";
				result += std::to_string(j);
				result += ") = ";
        
        // use get here. getThreadSafe is not required. It is only required for LLC implementation. This is private cache = high performance
				result += std::to_string(multiLevelCache.get(j)); // reading cache in multithreaded, up to 2.5 billion "get" per second
				result += "\r\n";

			}
			std::cout<<result<<std::endl;
		}
    return 0;
}
