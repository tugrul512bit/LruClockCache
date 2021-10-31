int main()
{
 		const int N=400000;
	
		// backing-store simulation
		std::vector<int> data(N);
	
		// 1 consumer, 8 producers
		// 180 million lookups per second
		AsyncCache<int,int> cache(
			1024*1024*4, // tags of direct mapped cache
			1024*1024*8, // tags of LRU (approximation)
			[&](int key){ return data[key]; }, // read miss
			[&](int key, int value){ data[key]=value; } // write miss
			8 // number of slots (producers)
		);
	
		/*
		1-2 million lookups per second
		methods of this class do not take slot id. each consumer has only 1 slot so remaining codes do not require slot id parameter with this object
		ZenithCache<int,int> cache(
			1024*1024, // L1 tags
			1024*1024, // L2 tags
			2, // consumer shards
			[&](int key){ return data[key]; },
			[&](int key, int value){ data[key]=value; }
		);
		
		
		
		*/
	

		std::cout<<"test1"<<std::endl;
		for(int i=0;i<100;i++)
		{
			CpuBenchmarker bench((N*1000)*sizeof(int),"setAsync",N*1000);
			#pragma omp parallel for
			for(int i=0;i<N/10;i++)
			{
				for(int j=0;j<10000;j++)
					cache.setAsync(i+j,i+j, /*i*/ omp_get_thread_num());
				cache.barrier(omp_get_thread_num());
			}
		}

		std::vector<int> out(N);
		for(int i=0;i<100;i++)
		{
			CpuBenchmarker bench((N*1000)*sizeof(int),"getAsync",N*1000);

			#pragma omp parallel for
			for(int i=0;i<N/10;i++)
			{
				for(int j=0;j<10000;j++)
					cache.getAsync(i+j,&out[i+j], /*i*/ omp_get_thread_num());
				cache.barrier(omp_get_thread_num());
			}
		}
		std::cout<<"test2"<<std::endl;

		std::cout<<"test3"<<std::endl;
		for(int i=0;i<N/10;i++)
		{
			if(out[i]!=i) std::cout<<"error"<<std::endl;
		}
		return 0; 
}
