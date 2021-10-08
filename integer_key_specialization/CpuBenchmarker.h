/*
 * CpuBenchmarker.h
 *
 *  Created on: Feb 21, 2021
 *      Author: tugrul
 */

#ifndef CPUBENCHMARKER_H_
#define CPUBENCHMARKER_H_

#include <chrono>
#include <string>
#include <iostream>
#include <iomanip>

// RAII type benchmarker
class CpuBenchmarker
{
public:
	CpuBenchmarker():CpuBenchmarker(0,"",0)
	{
		measurementTarget=nullptr;
	}

	CpuBenchmarker(size_t bytesToBench):CpuBenchmarker(bytesToBench,"",0)
	{
		measurementTarget=nullptr;
	}

	CpuBenchmarker(size_t bytesToBench, std::string infoExtra):CpuBenchmarker(bytesToBench,infoExtra,0)
	{
		measurementTarget=nullptr;
	}

	CpuBenchmarker(size_t bytesToBench, std::string infoExtra, size_t countForThroughput):t1(std::chrono::duration_cast< std::chrono::nanoseconds >(std::chrono::high_resolution_clock::now().time_since_epoch()))
	{
		bytes=bytesToBench;
		info=infoExtra;
		count = countForThroughput;
		measurementTarget=nullptr;
	}

	// writes elapsed time (in seconds) to this variable upon destruction
	void addTimeWriteTarget(double * measurement)
	{
		measurementTarget=measurement;
	}

	~CpuBenchmarker()
	{
		std::chrono::nanoseconds t2 =  std::chrono::duration_cast< std::chrono::nanoseconds >(std::chrono::high_resolution_clock::now().time_since_epoch());
		size_t t = t2.count() - t1.count();
		if(measurementTarget!=nullptr)
		{
			*measurementTarget=t/1000000000.0; // seconds
		}
		if(info!=std::string(""))
			std::cout<<info<<": ";
		std::cout<<t<<" nanoseconds    ";
		if(bytes>0)
		{
			std::cout <<" (bandwidth = ";
		    std::cout << std::fixed;
		    std::cout << std::setprecision(2);
			std::cout <<   (bytes/(((double)t)/1000000000.0))/1000000.0 <<" MB/s)     ";
		}
		if(count>0)
		{
			std::cout<<" (throughput = ";
		    std::cout << std::fixed;
		    std::cout << std::setprecision(2);
			std::cout <<   (((double)t)/count) <<" nanoseconds per iteration) ";
		}
		std::cout<<std::endl;
	}

private:
	std::chrono::nanoseconds t1;
	size_t bytes;
	size_t count;
	std::string info;
	double * measurementTarget;
};

#endif /* CPUBENCHMARKER_H_ */
