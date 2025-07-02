#pragma once

#include"Common.h"

class ThreadCache
{
public:
	// 申请和释放空间
	void* Allocate(size_t bytes);
	void Deallocate(void* ptr, size_t bytes);
	// 从中心缓存获取对象
	void* FetchFromCentralCache(size_t index, size_t bytes);
	
	void ListToLong(FreeList& list, size_t bytes);
private:
	FreeList _freeLists[NFREELISTS];
};

//线程局部存储 TLS
static __declspec(thread) ThreadCache* pTLSThreadCache = nullptr;