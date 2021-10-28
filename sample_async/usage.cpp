

/*
std::vector<int> data(1000000);
		AsyncCache<int,int> cache(128,128,1024,[&](int key){ return data[key]; },[&](int key, int value){ data[key]=value; });
		int val;
		int slot = cache.setAsync(5,100);
		cache.getAsync(5,&val,slot);
		std::cout<<data[5]<<" "<<val<<std::endl;
		cache.barrier(slot);
		std::cout<<data[5]<<" "<<val<<std::endl;
		cache.flush();
		std::cout<<data[5]<<" "<<val<<std::endl;
*/
