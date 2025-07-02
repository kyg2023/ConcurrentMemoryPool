#pragma once

#include"Common.h"

//定长内存池
//template<size_t N>
//class ObjectPool
//{};
template<class T>
class ObjectPool
{
public:
	T* New()
	{
		T* obj = nullptr;
		//优先重复利用还回来的内存
		if (_freeList)
		{
			void* next = *(void**)_freeList;
			obj = (T*)_freeList;
			_freeList = next;
		}
		//没用还回来的内存可重复利用再取未使用过的剩余内存
		else
		{
			size_t objSize = sizeof(T) < sizeof(void*) ? sizeof(void*) : sizeof(T);
			if (_remainBytes < objSize)//剩余内存不足一个对象大小
			{
				_remainBytes = objSize * 1024;
				//_memory = (char*)malloc(_remainBytes);
				_memory = (char*)SystemAlloc(_remainBytes >> PAGE_SHIFT);//摆脱使用malloc
				if (_memory == nullptr)
				{
					throw std::bad_alloc();
				}
			}
			obj = (T*)_memory;
			//至少给一个指针大小的内存，使其被归还后足以作下一个被归还内存节点的指针
			_memory += objSize;
			_remainBytes -= objSize;
		}

		//定位new，显示调用T的构造函数初始化
		new(obj)T;
		return obj;
	}

	void Delete(T* obj)
	{
		//显示调用析构函数
		obj->~T();

		//头插
		//*(int*)obj = _freeList;//用一个int大小的内存来存下一个内存节点的地址，无法兼容32/64位平台
		*(void**)obj = _freeList;//用一个void*大小(即一个指针大小)的内存来存下一个内存节点的地址，一个指针的大小随平台自动变动
		_freeList = obj;
	}

private:
	char* _memory = nullptr;//指向大块内存
	void* _freeList = nullptr;//还回来小内存块链表的头指针
	size_t _remainBytes = 0;//大块内存切分剩余的内存字节数
};




struct TreeNode
{
	int _val;
	TreeNode* _left;
	TreeNode* _right;
	TreeNode()
		:_val(0)
		, _left(nullptr)
		, _right(nullptr)
	{}
};

static void TestObjectPool()
{
	const size_t N = 500000;//申请释放次数

	//new/delete性能测试
	size_t begin1 = clock();
	std::vector<TreeNode*> v1;
	v1.reserve(N);
	for (int i = 0; i < N; ++i)
	{
		v1.push_back(new TreeNode);
	}
	for (int i = 0; i < N; ++i)
	{
		delete v1[i];
	}
	size_t end1 = clock();

	//ObjectPool性能测试
	ObjectPool<TreeNode> TNPool;
	size_t begin2 = clock();
	std::vector<TreeNode*> v2;
	v2.reserve(N);
	for (int i = 0; i < N; ++i)
	{
		v2.push_back(TNPool.New());
	}
	for (int i = 0; i < N; ++i)
	{
		TNPool.Delete(v2[i]);
	}
	size_t end2 = clock();

	cout << "new/delete cost time:" << end1 - begin1 << endl;
	cout << "ObjectPool cost time:" << end2 - begin2 << endl;
}