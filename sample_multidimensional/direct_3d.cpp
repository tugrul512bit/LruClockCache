#include "../integer_key_specialization/DirectMapped2DMultiThreadCache.h"
#include<iostream>

int main()
{
	int backingStore[10][10][10];
	DirectMapped3DMultiThreadCache<int,int> cache(4,4,4,
			[&](int x, int y, int z){ return backingStore[x][y][z]; },
			[&](int x, int y, int z, int value){  backingStore[x][y][z]=value; });
	for(int i=0;i<10;i++)
		for(int j=0;j<10;j++)
			for(int k=0;k<10;k++)
			cache.set(i,j,k,i+j+k); // depth major

	cache.flush();
	std::cout<<"-------------"<<std::endl;

		for(int i=0;i<10;i++)for(int j=0;j<10;j++)for(int k=0;k<10;k++)
			std::cout<<backingStore[i][j][k]<<std::endl;
	return 0;
}
