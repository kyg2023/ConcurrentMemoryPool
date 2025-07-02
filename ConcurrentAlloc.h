#pragma once

#include"Common.h"
#include"ThreadCache.h"
#include"PageCache.h"
#include"ObjectPool.h"

static void* ConcurrentAlloc(size_t bytes)
{
	if (bytes > MAX_BYTES)//大于256k
	{
		size_t alignBytes = SizeClass::RoundUp(bytes);
		size_t kpage = alignBytes >> PAGE_SHIFT;
		PageCache::GetInstance()->_pageMtx.lock();//若大于256k小于128页，会访问PageCache，需要加锁
		Span* span = PageCache::GetInstance()->NewSpan(kpage);
		PageCache::GetInstance()->_pageMtx.unlock();
		span->_objSize = bytes;

		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		return ptr;
	}
	else
	{
		// 通过TLS每个线程无锁的获取自己的专属ThreadCache对象
		if (pTLSThreadCache == nullptr)
		{
			//pTLSThreadCache = new ThreadCache;
			static ObjectPool<ThreadCache> tcPool;
			pTLSThreadCache = tcPool.New();//         加锁？
		}

		//cout << "线程(id " << std::this_thread::get_id() << ")的" << "pTLSThreadCache" << " : " << pTLSThreadCache << endl;

		return pTLSThreadCache->Allocate(bytes);
	}
	
}

static void ConcurrentFree(void* ptr)
{
	Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
	size_t bytes = span->_objSize;
	if (bytes > MAX_BYTES)//大于256k
	{
		PageCache::GetInstance()->_pageMtx.lock();//若大于256k小于128页，会访问PageCache，需要加锁
		PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		PageCache::GetInstance()->_pageMtx.unlock();
	}
	else
	{
		assert(pTLSThreadCache);
		pTLSThreadCache->Deallocate(ptr, bytes);
	}
}