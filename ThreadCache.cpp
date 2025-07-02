#include"ThreadCache.h"
#include"CentralCache.h"

void* ThreadCache::FetchFromCentralCache(size_t index, size_t bytes)
{
	//慢开始反馈调节算法
	//1.一开始不会一次向CentralCache批量要太多，防止用不完浪费
	//2.如果不断有bytes大小内存的需求，batchNum就会不断增长，直到上限SizeClass::NumMoveSize(bytes)
	//3.bytes越小，一次向CentralCache要的内存就越大
	//4.bytes越大，一次向CentralCache要的内存就越小
	size_t batchNum = min(_freeLists[index].MaxSize(), SizeClass::NumMoveSize(bytes));//threadCache一次从中心缓存获取多少个对象
	if (batchNum == _freeLists[index].MaxSize())
	{
		_freeLists[index].MaxSize() += 1;
	}
	
	void* start = nullptr;//输出型参数
	void* end = nullptr;//输出型参数  记录申请出的内存的首尾地址
	size_t actualNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, bytes);//实际要到的数量
	assert(actualNum >= 1);
	
	if (actualNum == 1)
	{
		assert(start == end);
		return start;//只申请到一个，直接返回，不用插入
	}
	else
	{
		//返回第一个内存对象，剩余的内存对象头插入到ThreadCache的freeLists[index]
		_freeLists[index].PushRange(NextObj(start), end, actualNum - 1);
		return start;
	}
}

void* ThreadCache::Allocate(size_t bytes)
{
	assert(bytes <= MAX_BYTES);
	size_t alignBytes = SizeClass::RoundUp(bytes);
	size_t index = SizeClass::Index(bytes);
	if (!_freeLists[index].Empty())
	{
		return _freeLists[index].Pop();
	}
	else
	{
		return FetchFromCentralCache(index, alignBytes);
	}
}

void ThreadCache::Deallocate(void* ptr, size_t bytes)
{
	assert(ptr);
	assert(bytes <= MAX_BYTES);

	// 找对应的自由链表桶，把对象头插进去
	size_t index = SizeClass::Index(bytes);
	_freeLists[index].Push(ptr);
	// 当链表长度大于一次批量申请的内存数量时就开始释放归还一段list给CentralCache
	if (_freeLists[index].Size() >= _freeLists[index].MaxSize())
	{
		ListToLong(_freeLists[index], bytes);
	}
}

void ThreadCache::ListToLong(FreeList& list, size_t bytes)
{
	void* start = nullptr;
	void* end = nullptr;//start、end输出性参数
	list.PopRange(start, end, list.MaxSize());
	CentralCache::GetInstance()->ReleaseListToSpans(start, bytes);
}
