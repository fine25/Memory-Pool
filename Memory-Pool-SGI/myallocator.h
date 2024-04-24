
#pragma once
#include <mutex>
#include <iostream>




// 封装了malloc和free，设置OOM释放内存的回调函数
template <int __inst>
class __malloc_alloc_template {
private:
	static void* _S_oom_malloc(size_t);
	static void* _S_oom_realloc(void*, size_t);
	static void (*__malloc_alloc_oom_handler)();

public:
	static void* allocate(size_t __n)
	{
		void* __result = malloc(__n);
		if (0 == __result) __result = _S_oom_malloc(__n);
		return __result;
	}

	static void deallocate(void* __p, size_t /* __n */)
	{
		free(__p);
	}

	static void* reallocate(void* __p, size_t /* old_sz */, size_t __new_sz)
	{
		void* __result = realloc(__p, __new_sz);
		if (0 == __result) __result = _S_oom_realloc(__p, __new_sz);
		return __result;
	}

	static void (*__set_malloc_handler(void (*__f)()))()
	{
		void (*__old)() = __malloc_alloc_oom_handler;
		__malloc_alloc_oom_handler = __f;
		return(__old);
	}

};
template <int __inst>
void (*__malloc_alloc_template<__inst>::__malloc_alloc_oom_handler)() = nullptr;

template <int __inst>
void* __malloc_alloc_template<__inst>::_S_oom_malloc(size_t __n)
{
	void (*__my_malloc_handler)();
	void* __result;

	for (;;) {
		__my_malloc_handler = __malloc_alloc_oom_handler;
		if (0 == __my_malloc_handler) { throw std::bad_alloc(); }////bad_allock带有字符串构造底层是私有化
		(*__my_malloc_handler)();
		__result = malloc(__n);
		if (__result) return(__result);
	}
}

template <int __inst>
void* __malloc_alloc_template<__inst>::_S_oom_realloc(void* __p, size_t __n)
{
	void (*__my_malloc_handler)();
	void* __result;

	for (;;) {
		__my_malloc_handler = __malloc_alloc_oom_handler;
		if (0 == __my_malloc_handler) { throw std::bad_alloc(); }
		(*__my_malloc_handler)();
		__result = realloc(__p, __n);
		if (__result) return(__result);
	}
}

typedef __malloc_alloc_template<0> malloc_alloc;

template<typename T>
class SGIAllocator {

public:
	using value_type = T;

	constexpr SGIAllocator() noexcept {}
	constexpr SGIAllocator(const SGIAllocator&) noexcept = default;
	template <class _Other>
	constexpr SGIAllocator(const SGIAllocator<_Other>&) noexcept {}

	// 开辟chunk块内存
	T* allocate(size_t __n) {
		// 调用allocate传入的是元素的个数量
		__n *= sizeof(T);//所以要*T类型的字节得到总体字节数
		void* __ret = 0;

		if (__n > (size_t)_MAX_BYTES) {
			__ret = malloc_alloc::allocate(__n);
		}
		else {
			_Obj* volatile* __my_free_list = _S_free_list + _S_freelist_index(__n);

			// 出作用域可自动析构解锁
			std::lock_guard<std::mutex> guard(mtx);

			_Obj* __result = *__my_free_list;
			if (__result == 0)
				__ret = _S_refill(_S_round_up(__n));
			else {
				*__my_free_list = __result->_M_free_list_link;
				__ret = __result;
			}
		}

		return (T*)__ret;
	}

	// 归还chunk块释放内存
	void deallocate(void* __p, size_t __n) {
		if (__n > (size_t)_MAX_BYTES)
			malloc_alloc::deallocate(__p, __n);
		else {
			_Obj* volatile* __my_free_list
				= _S_free_list + _S_freelist_index(__n);
			_Obj* __q = (_Obj*)__p;

			std::lock_guard<std::mutex> guard(mtx);

			__q->_M_free_list_link = *__my_free_list;
			*__my_free_list = __q;
		}
	}
	// 内存 扩容and缩容
	static void* reallocate(void* __p, size_t __old_sz, size_t __new_sz) {
		void* __result;
		size_t __copy_sz;

		if (__old_sz > (size_t)_MAX_BYTES && __new_sz > (size_t)_MAX_BYTES) {
			return(realloc(__p, __new_sz));
		}
		if (_S_round_up(__old_sz) == _S_round_up(__new_sz)) return(__p);
		__result = allocate(__new_sz);
		__copy_sz = __new_sz > __old_sz ? __old_sz : __new_sz;
		memcpy(__result, __p, __copy_sz);
		deallocate(__p, __old_sz);
		return(__result);
	}
	// 对象构造
	void construct(T* __p, const T& __val) {
		new (__p) T(__val);
	}
	// 对象析构
	void destroy(T* __p) {
		__p->~T();
	}
private:
	enum { _ALIGN = 8 };          // 自由链表从8字节开始，以8字节对齐，一直扩充到128
	enum { _MAX_BYTES = 128 };    // 内存池最大的chunk块
	enum { _NFREELISTS = 16 };    // 自由链表的节点个数

	// Chunk allocation state内存已分配chunk块使用情况

	static char* _S_start_free;//内存池备用可分配空间的起始地址
	static char* _S_end_free;//内存池备用可分配空间的末尾地址
	static size_t _S_heap_size;

	// chunk块的头信息，_M_free_list_link下一个chunk块的地址信息
	union _Obj {
		union _Obj* _M_free_list_link;
		char _M_client_data[1];    
	};

	// _S_free_list表示存储自由链表数组的起始地址
	static _Obj* volatile _S_free_list[_NFREELISTS];

	// 基于freelist实现，加锁考虑线程安全
	static std::mutex mtx;

	// 把__bytes的大小上调至8的整数倍返回
	static size_t _S_round_up(size_t __bytes) {
		return (((__bytes)+(size_t)_ALIGN - 1) & ~((size_t)_ALIGN - 1));
	}

	// 返回 __bytes 大小的chunk块位于 free-list 中的编号
	static size_t _S_freelist_index(size_t __bytes) {
		return (((__bytes)+(size_t)_ALIGN - 1) / (size_t)_ALIGN - 1);
	}



		//分配空间，把分配好的chunk块进行连接
	static void* _S_refill(size_t __n) {
		int __nobjs = 20;

		char* __chunk = _S_chunk_alloc(__n, __nobjs);
		_Obj* volatile* __my_free_list;
		_Obj* __result;
		_Obj* __current_obj;
		_Obj* __next_obj;
		int __i;

		if (1 == __nobjs) return(__chunk);	//备用chunk块空间只够一个分配
		__my_free_list = _S_free_list + _S_freelist_index(__n);

		/* Build free list in chunk */
		__result = (_Obj*)__chunk;
		*__my_free_list = __next_obj = (_Obj*)(__chunk + __n);
		for (__i = 1; ; __i++) {
			__current_obj = __next_obj;
			__next_obj = (_Obj*)((char*)__next_obj + __n);
			if (__nobjs - 1 == __i) {
				__current_obj->_M_free_list_link = 0;
				break;
			}
			else {
				__current_obj->_M_free_list_link = __next_obj;
			}
		}
		return(__result);
	}

	//主要负责分配自由链表chunk
	static char* _S_chunk_alloc(size_t __size, int& __nobjs) {
		char* __result = nullptr;
		size_t __total_bytes = __size * __nobjs;
		size_t __bytes_left = _S_end_free - _S_start_free; //查看备用空间

		if (__bytes_left >= __total_bytes) {
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}
		else if (__bytes_left >= __size) {
			__nobjs = (int)(__bytes_left / __size);
			__total_bytes = __size * __nobjs;
			__result = _S_start_free;
			_S_start_free += __total_bytes;
			return(__result);
		}//如果还有剩余，会分配到其他相应的chunk块
		else {
			size_t __bytes_to_get = 2 * __total_bytes + _S_round_up(_S_heap_size >> 4);
			// Try to make use of the left-over piece.
			if (__bytes_left > 0) {
				_Obj* volatile* __my_free_list =
					_S_free_list + _S_freelist_index(__bytes_left);

				((_Obj*)_S_start_free)->_M_free_list_link = *__my_free_list;
				*__my_free_list = (_Obj*)_S_start_free;
			}
			_S_start_free = (char*)malloc(__bytes_to_get);
			if (nullptr == _S_start_free) {
				size_t __i;
				_Obj* volatile* __my_free_list;
				_Obj* __p;

				for (__i = __size;
					__i <= (size_t)_MAX_BYTES;
					__i += (size_t)_ALIGN) {
					__my_free_list = _S_free_list + _S_freelist_index(__i);
					__p = *__my_free_list;
					if (nullptr != __p) {
						*__my_free_list = __p->_M_free_list_link;
						_S_start_free = (char*)__p;
						_S_end_free = _S_start_free + __i;
						return(_S_chunk_alloc(__size, __nobjs));
					}
				}
				_S_end_free = 0;
				_S_start_free = (char*)malloc_alloc::allocate(__bytes_to_get);
				// This should either throw an exception or remedy the situation.  
				// Thus we assume it succeeded.
			}
			_S_heap_size += __bytes_to_get;
			_S_end_free = _S_start_free + __bytes_to_get;
			return(_S_chunk_alloc(__size, __nobjs));
		}
	}


};


template <typename T>
char* SGIAllocator<T>::_S_start_free = nullptr;

template <typename T>
char* SGIAllocator<T>::_S_end_free = nullptr;

template <typename T>
size_t SGIAllocator<T>::_S_heap_size = 0;


template <typename T>
// typename告诉编译器_Obj是类型定义
typename SGIAllocator<T>::_Obj* volatile SGIAllocator<T>::_S_free_list[_NFREELISTS] = {
	nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,
	nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr
};

template <typename T>
std::mutex SGIAllocator<T>::mtx;