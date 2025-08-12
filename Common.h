#pragma once

//�߲����ڴ����Ҫ�������¼���������⡣
//1. �������⡣
//2. ���̻߳����£����������⡣
//3. �ڴ���Ƭ���⡣
//concurrent memory pool��Ҫ������3�����ֹ��ɣ�
//1. thread cache���̻߳�����ÿ���̶߳��еģ�����С��256KB���ڴ�ķ��䣬�̴߳����������ڴ治��Ҫ������
//		ÿ���̶߳���һ��cache����Ҳ������������̳߳ظ�Ч�ĵط���
//2. central cache�����Ļ����������߳�������thread cache�ǰ����central cache�л�ȡ�Ķ���
//		central cache���ʵ�ʱ������thread cache�еĶ��󣬱���һ���߳�ռ����̫����ڴ棬
//		�������̵߳��ڴ�Խ����ﵽ�ڴ�����ڶ���߳��и�����İ�����ȵ�Ŀ�ġ�
//		central cache�Ǵ��ھ����ģ����Դ�����ȡ�ڴ��������Ҫ���������������õ���Ͱ����
//		���ֻ��thread cache��û���ڴ����ʱ�Ż���central cache���������ﾺ������ܼ��ҡ�
//3. page cache��ҳ��������central cache���������һ�㻺�棬�洢���ڴ�����ҳΪ��λ�洢������ģ�
//		central cacheû���ڴ����ʱ����page cache�����һ��������page�����и�ɶ�����С��С���ڴ棬�����central cache��
//		��һ��span�ļ������ҳ�Ķ��󶼻����Ժ�page cache�����central cache����������span���󣬲��Һϲ����ڵ�ҳ����ɸ����ҳ�������ڴ���Ƭ�����⡣

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

//WIN32���� ��_WIN32��û��_WIN64
//WIN64���� ��_WIN32����_WIN64
//����_WIN64�жϷ���ǰ��
#ifdef _WIN64
	typedef unsigned long long PAGE_ID;
#elif _WIN32
	typedef size_t PAGE_ID;
#else
	#if defined(__x86_64__) || defined(__aarch64__)  // 64 λ�ܹ����
		typedef unsigned long long PAGE_ID;          // 64 λϵͳʹ�� 8 �ֽ�����
	#elif defined(__i386__) || defined(__arm__)       // 32 λ�ܹ����
		typedef size_t PAGE_ID;                      // 32 λϵͳʹ�� size_t��4 �ֽڣ�
	#else
		#error "Unsupported platform or architecture!"  // ��֧�ֵ�ƽ̨�����������
	#endif
#endif 


#ifdef _WIN32
	#include<windows.h>
#else
	//linux��brk mmap�ȵ�ͷ�ļ�
	#include <sys/mman.h>  // mmap, munmap
	#include <unistd.h>     // sysconf(_SC_PAGESIZE)
	//  Linux �µ��� munmap(ptr, page_size) ����֪�������С������ֻ�ܵ�ҳ�ͷ�
	// ȫ��ӳ����¼�����С��ָ�� -> ҳ����
	static std::unordered_map<void*, size_t> s_allocationMap;
	static std::mutex s_mutex;  // ����ӳ���Ļ�����
#endif

//ֱ��ȥ���ϰ�ҳ����ռ䣬����malloc
inline static void* SystemAlloc(size_t kpage)//ҳ�� 
{
	if (kpage == 0) return nullptr;
#ifdef _WIN32
	//������
	//1.�����ڴ�����ĵ�ַ,��nullptr��ʾ��ϵͳ����
	//2.Ҫ������߱���������Ĵ�С�����ֽ�Ϊ��λ  
	//3.��������
	//4.����������ķ��ʱ�����ʽ
	void* ptr = VirtualAlloc(nullptr, kpage << PAGE_SHIFT, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
	// Linux ʹ�� mmap
	//size_t page_size = sysconf(_SC_PAGESIZE); // ��ȡϵͳҳ��С
	//size_t total_size = kpage * page_size;
	size_t total_size = kpage << PAGE_SHIFT;
	void* ptr = mmap(
		nullptr,
		total_size,
		PROT_READ | PROT_WRITE,              // ��дȨ��
		MAP_PRIVATE | MAP_ANONYMOUS,          // ˽������ӳ�䣨�������ļ���
		-1, 0
	);
#endif
	// ͳһ������
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

//���ڴ�ֱ�ӹ黹���ѣ�����free
inline static void SystemFree(void* ptr)
{
	if (!ptr) return;
#ifdef _WIN32
	VirtualFree(ptr, 0, MEM_RELEASE);
#else
	// sbrk unmmap��
	size_t kpage = 0;
	{
		std::lock_guard<std::mutex> lock(s_mutex);
		auto it = s_allocationMap.find(ptr);
		if (it == s_allocationMap.end()) {
			std::cerr << "SystemFree error: Unknown pointer " << ptr << "\n";
			return;  // �����ظ��ͷŻ���Чָ��
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

// �����зֺõ�С�ڴ����������
class FreeList
{
public:
	void Push(void* obj)
	{
		assert(obj);

		//ͷ��
		//*(void**)obj = _freeList;
		NextObj(obj) = _freeList;
		_freeList = obj;
		++_size;
	}
	void* Pop()
	{
		assert(_freeList);

		//ͷɾ
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
	size_t _maxSize = 1;		//ThreadCache��CentralCacheһ�����������ڴ����ʱ�����������������һ����ʼ
	size_t _size = 0;			//����С���ڴ�������
};

// ��������С�Ķ���ӳ�����
class SizeClass
{
public:
	// ȫ����8����freeList̫�࣬�ֶ����ö�����
	// [1,128]					8byte����       freelist[0,16)        16��Ͱ
	// [128+1,1024]				16byte����		freelist[16,72)		  56��Ͱ
	// [1024+1,8*1024] 			128byte����		freelist[72,128)      56��Ͱ
	// [8*1024+1,64*1024]		1024byte����    freelist[128,184)	  56��Ͱ
	// [64*1024+1,256*1024]		8*1024byte����  freelist[184,208)	  24��Ͱ    ��208��Ͱ
	
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
		return (bytes + (AlignNum - 1)) & ~(AlignNum - 1);//ֻ������AlignNum��2��n����
		//(bytes + (AlignNum - 1)) --- bytes��һ����������һ
		// & ~(AlignNum - 1) --- ��ȥ����Ĳ���
	}

	// ��������Ĵ�С
	static inline size_t RoundUp(size_t bytes)
	{
		if (bytes <= 128) return _RoundUp(bytes, 8);
		else if (bytes <= 1024) return _RoundUp(bytes, 16);
		else if (bytes <= 8 * 1024) return _RoundUp(bytes, 128);
		else if (bytes <= 64 * 1024) return _RoundUp(bytes, 1024);
		else if (bytes <= 256 * 1024) return _RoundUp(bytes, 8 * 1024);
		//����256k
		else return _RoundUp(bytes, 1 << PAGE_SHIFT);//��һҳ����
	}


	//static inline size_t _Index(size_t bytes, size_t AlignNum)
	//{
	//	if (bytes % AlignNum == 0)
	//		return bytes / AlignNum - 1;
	//	else
	//		return bytes / AlignNum;
	//}

	static inline size_t _Index(size_t bytes, size_t AlignShift)//AlignShift��������2Ϊ�׵�ָ��
	{
		return ((bytes + (1 << AlignShift) - 1) >> AlignShift) - 1;//�ȼ� (bytes + AlignNum - 1) / AlignNum - 1
	}

	// ����ӳ�����һ����������Ͱ
	static inline size_t Index(size_t bytes)
	{
		assert(bytes <= MAX_BYTES);
		// ÿ�������ж��ٸ���
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

	// threadCacheһ�δ����Ļ����ȡ���ٸ�����
	static size_t NumMoveSize(size_t bytes)
	{
		if (bytes == 0)
			return 0;
		// [2, 512]��һ�������ƶ����ٸ������(������)����ֵ
		// С����һ���������޸�
		// �����һ���������޵�
		int num = MAX_BYTES / bytes;
		if (num < 2)
			num = 2;
		if (num > 512)
			num = 512;
		return num;
	}

	// ����һ����ϵͳ��ȡ����ҳ
	static size_t NumMovePage(size_t bytes)
	{
		size_t num = NumMoveSize(bytes);
		size_t npage = num * bytes >> PAGE_SHIFT;//Ҫȡ�����ڴ��Сת����ҳ�� 
		if (npage == 0)//���ٸ�һҳ
			npage = 1;
		return npage;
	}
};


//����������ҳ����ڴ��Ƚṹ
struct Span
{
	PAGE_ID _pageId = 0;//����ڴ���ʼҳ��ҳ��
	size_t _n = 0;//ҳ��
	size_t _useCount = 0;//�кõ�С���ڴ汻�����thread cache��ʹ�ü���  �黹�ڴ�ʱ��Ϊ0��ʾ��ȫ���黹
	size_t _objSize = 0;//��span�к�С�ڴ��Ĵ�С
	bool _isUsed = false;//�Ƿ��ڱ�ʹ��


	void* _freeList = nullptr;//ָ��CetralCache���кõ�С���ڴ����������
	
	//˫������ṹ
	Span* _prev = nullptr;
	Span* _next = nullptr;
};

//��ͷ˫��ѭ������
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
		//��pos��SpanList���޳�
		Span* prev = pos->_prev;
		Span* next = pos->_next;
		prev->_next = next;
		next->_prev = prev;
		//��posָ��ǰ��ڵ��ָ���ÿ�
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
	std::mutex _mtx;//Ͱ��
};
