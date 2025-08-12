#include"ConcurrentAlloc.h"
#include<atomic>

//nworks ���� �߳���
//ntimes ���� ���ִ�������ͷ��ڴ�Ĵ���
//rounds ���� �ִ�
void BenchmarkMalloc(size_t nworks, size_t ntimes, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime(0);
	std::atomic<size_t> free_costtime(0);
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
					//v.push_back(malloc(16));//��������̶���С�ڴ�
					v.push_back(malloc((16 + i) % 8192 + 1));//�������벻����С�ڴ�
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
	printf("%u���̲߳���ִ��%u�ִΣ�ÿ�ִ�malloc %u��: ���ѣ�%u ms\n",
		nworks, rounds, ntimes, (unsigned int)malloc_costtime);
	printf("%u���̲߳���ִ��%u�ִΣ�ÿ�ִ�free %u��: ���ѣ�%u ms\n",
		nworks, rounds, ntimes, (unsigned int)free_costtime);
	printf("%u���̲߳���malloc&free %u�Σ��ܼƻ��ѣ�%u ms\n",
		nworks, nworks * rounds * ntimes, (unsigned int)malloc_costtime + (unsigned int)free_costtime);
}

void BenchmarkConcurrentMalloc(size_t nworks, size_t ntimes, size_t rounds)
{
	std::vector<std::thread> vthread(nworks);
	std::atomic<size_t> malloc_costtime(0);
	std::atomic<size_t> free_costtime(0);
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
					//v.push_back(ConcurrentAlloc(16));//��������̶��ڴ�
					v.push_back(ConcurrentAlloc((16 + i) % 8192 + 1));//�������벻���ڴ�
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
	printf("%u���̲߳���ִ��%u�ִΣ�ÿ�ִ�concurrent alloc %u��: ���ѣ�%u ms\n",
		nworks, rounds, ntimes, (unsigned int)malloc_costtime);
	printf("%u���̲߳���ִ��%u�ִΣ�ÿ�ִ�concurrent dealloc %u��: ���ѣ�%u ms\n",
		nworks, rounds, ntimes, (unsigned int)free_costtime);
	printf("%u���̲߳���concurrent alloc&dealloc %u�Σ��ܼƻ��ѣ�%u ms\n",
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
