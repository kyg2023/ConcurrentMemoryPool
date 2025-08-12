#pragma once

//高并发内存池需要考虑以下几方面的问题。
//1. 性能问题。
//2. 多线程环境下，锁竞争问题。
//3. 内存碎片问题。
//concurrent memory pool主要由以下3个部分构成：
//1. thread cache：线程缓存是每个线程独有的，用于小于256KB的内存的分配，线程从这里申请内存不需要加锁，
//		每个线程独享一个cache，这也就是这个并发线程池高效的地方。
//2. central cache：中心缓存是所有线程所共享，thread cache是按需从central cache中获取的对象。
//		central cache合适的时机回收thread cache中的对象，避免一个线程占用了太多的内存，
//		而其他线程的内存吃紧，达到内存分配在多个线程中更均衡的按需调度的目的。
//		central cache是存在竞争的，所以从这里取内存对象是需要加锁，首先这里用的是桶锁，
//		其次只有thread cache的没有内存对象时才会找central cache，所以这里竞争不会很激烈。
//3. page cache：页缓存是在central cache缓存上面的一层缓存，存储的内存是以页为单位存储及分配的，
//		central cache没有内存对象时，从page cache分配出一定数量的page，并切割成定长大小的小块内存，分配给central cache。
//		当一个span的几个跨度页的对象都回收以后，page cache会回收central cache满足条件的span对象，并且合并相邻的页，组成更大的页，缓解内存碎片的问题。

#include<iostream>
#include<vector>
#include<unordered_map>
#include<time.h>
#include<assert.h>
#include<thread>
#include<mutex>
#include<algorithm>

using std::cin;
using std::cout;
using std::endl;

static const size_t MAX_BYTES = 256 * 1024;  //256kb
static const size_t NFREELISTS = 208;
static const size_t NPAGES = 129;
static const size_t PAGE_SHIFT = 13;

//WIN32配置 有_WIN32，没有_WIN64
//WIN64配置 有_WIN32，有_WIN64
//所以_WIN64判断放在前面
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;
#else
	#if defined(__x86_64__) || defined(__aarch64__)  // 64 位架构检测
		typedef unsigned long long PAGE_ID;          // 64 位系统使用 8 字节类型
	#elif defined(__i386__) || defined(__arm__)       // 32 位架构检测
		typedef size_t PAGE_ID;                      // 32 位系统使用 size_t（4 字节）
	#else
		#error "Unsupported platform or architecture!"  // 不支持的平台触发编译错误
	#endif
#endif 


#ifdef _WIN32
	#include<windows.h>
#else
	//linux下brk mmap等的头文件
	#include <sys/mman.h>  // mmap, munmap
	#include <unistd.h>     // sysconf(_SC_PAGESIZE)
	//  Linux 下调用 munmap(ptr, page_size) 必须知道分配大小！否则只能单页释放
	// 全局映射表记录分配大小（指针 -> 页数）
	static std::unordered_map<void*, size_t> s_allocationMap;
	static std::mutex s_mutex;  // 保护映射表的互斥锁
#endif

//直接去堆上按页申请空间，脱离malloc
inline static void* SystemAlloc(size_t kpage)//页数 
{
	if (kpage == 0) return nullptr;
#ifdef _WIN32
	//参数：
	//1.分配内存区域的地址,设nullptr表示由系统决定
	//2.要分配或者保留的区域的大小，以字节为单位  
	//3.分配类型
	//4.被分配区域的访问保护方式
	void* ptr = VirtualAlloc(nullptr, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// Linux 使用 mmap
	//size_t page_size = sysconf(_SC_PAGESIZE); // 获取系统页大小
	//size_t total_size = kpage * page_size;
	size_t total_size = kpage << PAGE_SHIFT;
	void* ptr = mmap(
		nullptr,
		total_size,
		PROT_READ | PROT_WRITE,              // 读写权限
		MAP_PRIVATE | MAP_ANONYMOUS,          // 私有匿名映射（不关联文件）
		-1, 0
	);
#endif
	// 统一错误处理
	bool failed =
#ifdef _WIN32
	(ptr == nullptr);
#else
	(ptr == MAP_FAILED);
#endif
	if (failed) 
	{
		std::cerr << "SystemAlloc failed for " << kpage << " pages\n";
		throw std::bad_alloc();
	}
#ifndef _WIN32
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		s_allocationMap[ptr] = kpage;
	}
#endif
	return ptr;
}

//将内存直接归还给堆，脱离free
inline static void SystemFree(void* ptr)
{
	if (!ptr) return;
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap等
	size_t kpage = 0;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		auto it = s_allocationMap.find(ptr);
		if (it == s_allocationMap.end()) {
			std::cerr << "SystemFree error: Unknown pointer " << ptr << "\n";
			return;  // 避免重复释放或无效指针
		}
		kpage = it->second;
		s_allocationMap.erase(it);
	}
	size_t total_size = kpage << PAGE_SHIFT;
	if (munmap(ptr, total_size) == -1) 
		std::cerr << "SystemFree(munmap) failed for " << ptr << "\n";
#endif
}


static void*& NextObj(void* obj)
{
	return *(void**)obj;
}

// 管理切分好的小内存的自由链表
class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);

		//头插
		//*(void**)obj = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}
	void* Pop()
	{
		assert(_freeList);

		//头删
		void* obj = _freeList;
		_freeList = NextObj(obj);
		--_size;

		return obj;
	}

	void PushRange(void* start, void* end, size_t n)
	{
		NextObj(end) = _freeList;
		_freeList = start;
		_size += n;
	}
	void PopRange(void*& start, void*& end, size_t n)
	{
		assert(n >= _size);

		start = _freeList;
		end = start;
		for (size_t i = 0; i < n - 1; i++)
		{
			end = NextObj(end);
		}
		_freeList = NextObj(end);
		NextObj(end) = nullptr;
		_size -= n;
	}

	bool Empty()
	{
		return _freeList == nullptr;
	}

	size_t& MaxSize()
	{
		return _maxSize;
	}

	size_t Size()
	{
		return _size;
	}
private:
	void* _freeList = nullptr;
	size_t _maxSize = 1;		//ThreadCache向CentralCache一次批量申请内存对象时的最大申请数量，从一个开始
	size_t _size = 0;			//含有小块内存对象个数
};

// 计算对象大小的对齐映射规则
class SizeClass
{
public:
	// 全部按8对齐freeList太多，分段设置对齐数
	// [1,128]					8byte对齐       freelist[0,16)        16个桶
	// [128+1,1024]				16byte对齐		freelist[16,72)		  56个桶
	// [1024+1,8*1024] 			128byte对齐		freelist[72,128)      56个桶
	// [8*1024+1,64*1024]		1024byte对齐    freelist[128,184)	  56个桶
	// [64*1024+1,256*1024]		8*1024byte对齐  freelist[184,208)	  24个桶    共208个桶
	
	//static inline size_t _Roundup(size_t bytes, size_t AlignNum)
	//{
	//	size_t AlignBytes;
	//	if (bytes % AlignNum != 0)
	//	{
	//		AlignBytes = (bytes / AlignNum + 1) * AlignNum;
	//	}
	//	else
	//	{
	//		AlignBytes = bytes;
	//	}
	//	return AlignBytes;
	//}
	static inline size_t _RoundUp(size_t bytes, size_t AlignNum)
	{
		return (bytes + (AlignNum - 1)) & ~(AlignNum - 1);//只适用于AlignNum是2的n次幂
		//(bytes + (AlignNum - 1)) --- bytes加一个对齐数减一
		// & ~(AlignNum - 1) --- 消去多余的部分
	}

	// 计算对齐后的大小
	static inline size_t RoundUp(size_t bytes)
	{
		if (bytes <= 128) return _RoundUp(bytes, 8);
		else if (bytes <= 1024) return _RoundUp(bytes, 16);
		else if (bytes <= 8 * 1024) return _RoundUp(bytes, 128);
		else if (bytes <= 64 * 1024) return _RoundUp(bytes, 1024);
		else if (bytes <= 256 * 1024) return _RoundUp(bytes, 8 * 1024);
		//大于256k
		else return _RoundUp(bytes, 1 << PAGE_SHIFT);//以一页对齐
	}


	//static inline size_t _Index(size_t bytes, size_t AlignNum)
	//{
	//	if (bytes % AlignNum == 0)
	//		return bytes / AlignNum - 1;
	//	else
	//		return bytes / AlignNum;
	//}

	static inline size_t _Index(size_t bytes, size_t AlignShift)//AlignShift对齐数以2为底的指数
	{
		return ((bytes + (1 << AlignShift) - 1) >> AlignShift) - 1;//等价 (bytes + AlignNum - 1) / AlignNum - 1
	}

	// 计算映射的哪一个自由链表桶
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// 每个区间有多少个链
		static int group_array[4] = { 16, 56, 56, 56 };
		if (bytes <= 128) {
			return _Index(bytes, 3);
		}
		else if (bytes <= 1024) {
			return _Index(bytes - 128, 4) + group_array[0];
		}
		else if (bytes <= 8 * 1024) {
			return _Index(bytes - 1024, 7) + group_array[1] + group_array[0];
		}
		else if (bytes <= 64 * 1024) {
			return _Index(bytes - 8 * 1024, 10) + group_array[2] + group_array[1] + group_array[0];
		}
		else if (bytes <= 256 * 1024) {
			return _Index(bytes - 64 * 1024, 13) + group_array[3] + group_array[2] + group_array[1] + group_array[0];
		}
		else {
			assert(false);
			return -1;
		}
	}

	// threadCache一次从中心缓存获取多少个对象
	static size_t NumMoveSize(size_t bytes)
	{
		if (bytes == 0)
			return 0;
		// [2, 512]，一次批量移动多少个对象的(慢启动)上限值
		// 小对象一次批量上限高
		// 大对象一次批量上限低
		int num = MAX_BYTES / bytes;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	// 计算一次向系统获取几个页
	static size_t NumMovePage(size_t bytes)
	{
		size_t num = NumMoveSize(bytes);
		size_t npage = num * bytes >> PAGE_SHIFT;//要取的总内存大小转化成页数 
		if (npage == 0)//至少给一页
			npage = 1;
		return npage;
	}
};


//管理多个连续页大块内存跨度结构
struct Span
{
	PAGE_ID _pageId = 0;//大块内存起始页的页号
	size_t _n = 0;//页数
	size_t _useCount = 0;//切好的小块内存被分配给thread cache的使用计数  归还内存时，为0表示已全部归还
	size_t _objSize = 0;//该span切好小内存块的大小
	bool _isUsed = false;//是否在被使用


	void* _freeList = nullptr;//指向CetralCache中切好的小块内存的自由链表
	
	//双向链表结构
	Span* _prev = nullptr;
	Span* _next = nullptr;
};

//带头双向循环链表
class SpanList
{
public:
	SpanList()
	{
		_head = new Span;
		_head->_next = _head;
		_head->_prev = _head;
	}

	void PushFront(Span* span)
	{
		Insert(Begin(), span);
	}

	Span* PopFront()
	{
		Span* front = _head->_next;
		Erase(front);
		return front;
	}

	void Insert(Span* pos, Span* newSpan)
	{
		assert(pos);
		assert(newSpan);

		Span* prev = pos->_prev;
		prev->_next = newSpan;
		newSpan->_prev = prev;
		newSpan->_next = pos;
		pos->_prev = newSpan;
	}

	void Erase(Span* pos)
	{
		assert(pos);
		assert(pos != _head);
		//将pos从SpanList中剔除
		Span* prev = pos->_prev;
		Span* next = pos->_next;
		prev->_next = next;
		next->_prev = prev;
		//将pos指向前后节点的指针置空
		pos->_prev = nullptr;
		pos->_next = nullptr;
	}

	Span* Begin()
	{
		return _head->_next;
	}
	Span* End()
	{
		return _head;
	}

	bool Empty()
	{
		return _head->_next == _head;
	}

private:
	Span* _head;
public:
	std::mutex _mtx;//桶锁
};
