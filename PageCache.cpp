#include"PageCache.h"

PageCache PageCache::_sInst;

// 分配一个k页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);

	if (k > NPAGES - 1)//若大于128页，直接找堆申请
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		//_idSpanMap[span->_pageId] = span;//建立_pageId与span的映射
		_idSpanMap.set(span->_pageId, (void*)span);

		return span;
	}

	//先查看k页的桶中有无非空span
	if (!_spanLists[k].Empty())
	{
		Span* kSpan = _spanLists[k].PopFront();
		//建立kSpan中所有页的_pageId和span的映射，方便CentralCache回收小块内存时查找对应的span
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			//_idSpanMap[kSpan->_pageId + i] = kSpan;
			_idSpanMap.set(kSpan->_pageId + i, (void*)kSpan);
		}
		return kSpan;
	}

	//检查一下后面大的桶有没有非空span，如果有可以把它切分
	for (size_t i = k + 1; i < NPAGES; i++)
	{
		if (!_spanLists[i].Empty())
		{
			Span* nSpan = _spanLists[i].PopFront();
			Span* kSpan = _spanPool.New();
			//在nSpan的头部切一个k页下来
			kSpan->_pageId = nSpan->_pageId;
			kSpan->_n = k;
			nSpan->_pageId += k;
			nSpan->_n -= k;

			_spanLists[nSpan->_n].PushFront(nSpan);

			//建立nSpan中首尾页页号跟nSpan的映射，方便合并span时查找前后相邻的span
			//_idSpanMap[nSpan->_pageId] = nSpan;
			//_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;
			_idSpanMap.set(nSpan->_pageId, (void*)nSpan);
			_idSpanMap.set(nSpan->_pageId + nSpan->_n - 1, (void*)nSpan);


			//建立kSpan中所有页的_pageId和span的映射，方便CentralCache回收小块内存时查找对应的span
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				//_idSpanMap[kSpan->_pageId + i] = kSpan;
				_idSpanMap.set(kSpan->_pageId + i, (void*)kSpan);
			}

			return kSpan;
		}
	}
	//后面大的桶也都没有非空span，从堆申请一个128页的span
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;//bigSpan起始页的页号
	bigSpan->_n = NPAGES - 1;//bigSpan包含的页数
	_spanLists[bigSpan->_n].PushFront(bigSpan);

	return NewSpan(k);
}


// 获取从对象到span的映射
Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = (PAGE_ID)obj >> PAGE_SHIFT;

	//std::unique_lock<std::mutex> lock(_pageMtx);//RAII锁 出了作用域自动解锁
	//auto it = _idSpanMap.find(id);//find时，其他线程可能在写，unordered_map插入一个元素结构可能会改变，需要加锁
	//if (it != _idSpanMap.end())
	//{
	//	return it->second;
	//}
	//else
	//{
	//	assert(false);
	//	return nullptr;
	//}

	//基数树不需要加锁
	Span* span = (Span*)_idSpanMap.get(id);
	assert(span);
	return span;
}

// 释放空间span回到PageCache，并合并相邻的span
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	//大于128页的span直接归还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
	}
	else//尝试对span前后页合并，解决内存碎片问题
	{
		while (1)//向前合并
		{
			PAGE_ID prevId = span->_pageId - 1;
			//auto it = _idSpanMap.find(prevId);
			//if (it == _idSpanMap.end())//前一页不存在，不合并
			//	break;
			//Span* prevSpan = it->second;
			Span* prevSpan = (Span*)_idSpanMap.get(prevId);
			if (prevSpan == nullptr)//前一页不存在，不合并
				break;

			if (prevSpan->_isUsed == true)//包含前一页的span在使用，不合并
				break;

			if (prevSpan->_n + span->_n > NPAGES - 1)//合起来超过128页不好管理，不合并
				break;

			span->_pageId = prevSpan->_pageId;
			span->_n += prevSpan->_n;
			_spanLists[prevSpan->_n].Erase(prevSpan); 
			//delete prevSpan;
			_spanPool.Delete(prevSpan);
		}
		while (1)//向后合并
		{
			PAGE_ID nextId = span->_pageId + span->_n;
			//auto it = _idSpanMap.find(nextId);
			//if (it == _idSpanMap.end())//后一页不存在，不合并
			//	break;
			//Span* nextSpan = it->second;
			Span* nextSpan = (Span*)_idSpanMap.get(nextId);
			if (nextSpan == nullptr)//后一页不存在，不合并
				break;

			if (nextSpan->_isUsed == true)//包含后一页的span在使用，不合并
				break;

			if (nextSpan->_n + span->_n > NPAGES - 1)//合起来超过128页不好管理，不合并
				break;

			span->_n += nextSpan->_n;
			_spanLists[nextSpan->_n].Erase(nextSpan);
			//delete nextSpan;
			_spanPool.Delete(nextSpan);
		}

		_spanLists[span->_n].PushFront(span);
		span->_isUsed = false;//更新为未使用状态
		//重新建立span首尾页页号跟span的映射关系
		//_idSpanMap[span->_pageId] = span;
		//_idSpanMap[span->_pageId + span->_n - 1] = span;
		_idSpanMap.set(span->_pageId, (void*)span);
		_idSpanMap.set(span->_pageId + span->_n - 1, (void*)span);
	}
}