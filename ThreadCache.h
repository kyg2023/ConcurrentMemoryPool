#pragma once

#include"Common.h"

class ThreadCache
{
public:
	// ������ͷſռ�
	void* Allocate(size_t bytes);
	void Deallocate(void* ptr, size_t bytes);
	// �����Ļ����ȡ����
	void* FetchFromCentralCache(size_t index, size_t bytes);
	
	void ListToLong(FreeList& list, size_t bytes);
private:
	FreeList _freeLists[NFREELISTS];
};

//�ֲ߳̾��洢 TLS
static thread_local ThreadCache* pTLSThreadCache = nullptr;
