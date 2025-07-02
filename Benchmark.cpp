#include"ConcurrentAlloc.h"
#include<atomic>

//nworks —— 线程数
//ntimes —— 单轮次申请和释放内存的次数
//rounds —— 轮次
void BenchmarkMalloc(size_t nworks, size_t ntimes, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(malloc(16));//测试申请固定大小内存
					v.push_back(malloc((16 + i) % 8192 + 1));//测试申请不定大小内存
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					free(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	printf("%u个线程并发执行%u轮次，每轮次malloc %u次: 花费：%u ms\n",
		nworks, rounds, ntimes, (unsigned int)malloc_costtime);
	printf("%u个线程并发执行%u轮次，每轮次free %u次: 花费：%u ms\n",
		nworks, rounds, ntimes, (unsigned int)free_costtime);
	printf("%u个线程并发malloc&free %u次，总计花费：%u ms\n",
		nworks, nworks * rounds * ntimes, (unsigned int)malloc_costtime + (unsigned int)free_costtime);
}

void BenchmarkConcurrentMalloc(size_t nworks, size_t ntimes, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime = 0;
	std::atomic<size_t> free_costtime = 0;
	for (size_t k = 0; k < nworks; ++k)
	{
		vthread[k] = std::thread([&]() {
			std::vector<void*> v;
			v.reserve(ntimes);
			for (size_t j = 0; j < rounds; ++j)
			{
				size_t begin1 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					//v.push_back(ConcurrentAlloc(16));//测试申请固定内存
					v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));//测试申请不定内存
				}
				size_t end1 = clock();
				size_t begin2 = clock();
				for (size_t i = 0; i < ntimes; i++)
				{
					ConcurrentFree(v[i]);
				}
				size_t end2 = clock();
				v.clear();
				malloc_costtime += (end1 - begin1);
				free_costtime += (end2 - begin2);
			}
			});
	}
	for (auto& t : vthread)
	{
		t.join();
	}
	printf("%u个线程并发执行%u轮次，每轮次concurrent alloc %u次: 花费：%u ms\n",
		nworks, rounds, ntimes, (unsigned int)malloc_costtime);
	printf("%u个线程并发执行%u轮次，每轮次concurrent dealloc %u次: 花费：%u ms\n",
		nworks, rounds, ntimes, (unsigned int)free_costtime);
	printf("%u个线程并发concurrent alloc&dealloc %u次，总计花费：%u ms\n",
		nworks, nworks * rounds * ntimes, (unsigned int)malloc_costtime + (unsigned int)free_costtime);
}
int main()
{
	size_t n = 10000;
	BenchmarkConcurrentMalloc(1, n, 10);
	cout << "==================================================================" << endl;
	BenchmarkMalloc(1, n, 10);

	return 0;
}
