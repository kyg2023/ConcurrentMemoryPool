#pragma once

#include"Common.h"

class CentralCache
{
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}

	// 分配一定数量的对象给ThreadCache
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes);
	// 从SpanLis获取一个非空的Span
	Span* GetOneSpan(SpanList& list, size_t byte_size);
	// 将一定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t bytes);

private:
	SpanList _spanLists[NFREELISTS];//桶数与ThreadCache相同

	//要求整个项目只有一个全局的CentralCache
	//单例模式 - 饿汉模式
private:
	CentralCache()
	{}
	CentralCache(const CentralCache&) = delete;

	static CentralCache _sInst;
};