#pragma once

#include"Common.h"
#include"ObjectPool.h"
#include"PageMap.h"

class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}

	// 分配一个k页的span
	Span* NewSpan(size_t k);
	// 获取从对象到span的映射
	Span* MapObjectToSpan(void* obj);
	// 释放空间span回到PageCache，并合并相邻的span
	void ReleaseSpanToPageCache(Span* span);

	std::mutex _pageMtx;
private:
	SpanList _spanLists[NPAGES];//下标是n的桶包含的就是含有n页的span
	//std::unordered_map<PAGE_ID, Span*> _idSpanMap;//内存页号与其所属span的映射
	//测试发现Span* MapObjectToSpan(void* obj)的锁竞争性能消耗很大
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;//用基数树代替unordered_map存储页号与span的映射关系来优化

	ObjectPool<Span> _spanPool;

	//要求整个项目只有一个全局的PageCache
	//单例模式 - 饿汉模式
private:
	PageCache()
	{}
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;
};