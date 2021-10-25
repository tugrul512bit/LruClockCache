#include "../integer_key_specialization/DirectMapped2DMultiThreadCache.h"
#include<iostream>

int main()
{
 		int backingStore[10][10];
		DirectMapped2DMultiThreadCache<int,int> cache(4,4,
				[&](int x, int y){ return backingStore[x][y]; },
				[&](int x, int y, int value){  backingStore[x][y]=value; });
		for(int i=0;i<10;i++)
			for(int j=0;j<10;j++)
				cache.set(i,j,i+j*10000); // row-major

		cache.flush();
		std::cout<<"-------------"<<std::endl;

			for(int j=0;j<10;j++)for(int i=0;i<10;i++)
				std::cout<<backingStore[i][j]<<std::endl;
		return 0; 
}
