

/*
std::vector<int> data(1000000);
		AsyncCache<int,int> cache(128,1024,[&](int key){ return data[key]; },[&](int key, int value){ data[key]=value; });
		int val;
		int slot = cache.setAsync(5,100); // or this: int slot = omp_get_thread_num(); cache.setAsync(5,100,slot);
		cache.getAsync(5,&val,slot);
		std::cout<<data[5]<<" "<<val<<std::endl;
		cache.barrier(slot);
		std::cout<<data[5]<<" "<<val<<std::endl;
		cache.flush();
		std::cout<<data[5]<<" "<<val<<std::endl;
*/
