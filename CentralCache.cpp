#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_sInst;

// 分配一定数量的对象给ThreadCache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t bytes)
{
	size_t index = SizeClass::Index(bytes);
	_spanLists[index]._mtx.lock();
	//获取一个非空Span
	Span* span = GetOneSpan(_spanLists[index], bytes);
	assert(span);
	assert(span->_freeList);
	//从span中头切出batchNum个对象给ThreadCache
	//若不够，有多少拿多少
	start = span->_freeList;//先切下一块做头，方便尾插
	end = start;
	size_t actualNum = 1;
	size_t i = 1;
	while (i < batchNum && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		++i;
		++actualNum;
	}
	span->_freeList = NextObj(end);//剩余的还给span
	NextObj(end) = nullptr;//切走的结尾置nullptr
	span->_useCount += actualNum;

	_spanLists[index]._mtx.unlock();
	return actualNum;
}

// 从SpanList获取一个非空的Span
Span* CentralCache::GetOneSpan(SpanList& list, size_t bytes)
{
	//先查看当前SpanList是否非空的Span
	Span* it = list.Begin();
	while (it != list.End())
	{
		if (it->_freeList != nullptr)
			return it;
		else
			it = it->_next;
	}
	//走到这里说明当前SpanList没用空闲的Span，只能找PageCache要
	list._mtx.unlock();//进入PageCache前，可以先把CentralCache的桶锁解掉，这样如果其他线程释放内存对象回来不会阻塞
	PageCache::GetInstance()->_pageMtx.lock();
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(bytes));
	span->_isUsed = true;//标志span已使用，防止释放内存合并span时，因为此span刚取过来还没切割_useCount为0又被拿回合并
	span->_objSize = bytes;
	PageCache::GetInstance()->_pageMtx.unlock();
	//由span中的_pageId和_n计算其_freeList应指向的大块内存的起始地址和大小字节数
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	size_t size = span->_n << PAGE_SHIFT;
	char* end = start + size; 
	//把span中大块内存切成自由链表连接起来
	span->_freeList = start;//先切下一块做头，方便尾插切分
	void* tail = span->_freeList;
	start += bytes;
	//int i = 1;//验证是否切成了想要的份数
	while (start < end)
	{
		//++i;
		NextObj(tail) = start;
		tail = NextObj(tail);
		start += bytes;
	}
	NextObj(tail) = nullptr;//结尾置空
	//切好span后，需要把span挂到CentralCache对应的桶里面，这里需要把桶锁加回
	list._mtx.lock();
	list.PushFront(span);

	return span;
}


void CentralCache::ReleaseListToSpans(void* start, size_t bytes)
{
	size_t index = SizeClass::Index(bytes);
	_spanLists[index]._mtx.lock();
	while (start)
	{
		void* next = NextObj(start);
		//每个小内存块可能来自不同的span，通过映射关系找到其所属span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);//获得小块内存所归属的span
		//直接将小块内存头插归还给span，不必处理小块内存的顺序
		NextObj(start) = span->_freeList;
		span->_freeList = start;
		--span->_useCount;
		if (span->_useCount == 0)//说明span切分出去的小块内存都回来了，可以进一步归还给PageCache，PageCache可以再做前后页的合并
		{
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_next = nullptr;
			span->_prev = nullptr;
			//保留span->_pageId，用于计算span所指向内存的起始地址
			_spanLists[index]._mtx.unlock();//释放span给PageCache时，使用PageCache的锁就可以了，可以暂时把桶锁解掉
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();
			_spanLists[index]._mtx.lock();
		}

		start = next;
	}
	_spanLists[index]._mtx.unlock();
}
