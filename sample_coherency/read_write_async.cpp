// outputs 5.5 nanoseconds per setAsync/getAsync on average as inverse throughput (~180 million lookups per second with single consumer thread, 8 producer threads)
// real latency is about 1000-2000 nanoseconds

int main()
{
 		const int N=400000;
		std::vector<int> data(N);
		AsyncCache<int,int> cache(1024*1024*4,1024*1024*8,[&](int key){ return data[key]; },[&](int key, int value){ data[key]=value; });
		int d=0;
		std::cin>>d;
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
