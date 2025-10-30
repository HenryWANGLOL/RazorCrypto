#pragma once

#include <cstdint>
#include <atomic>
#include <cstddef>
#include <stdlib.h>

namespace mht_rt
{

#ifdef __linux__
#define CAS(v, old_v, new_v) __sync_bool_compare_and_swap(v, old_v, new_v)
#define tb_mb() asm volatile("sfence" : : : "memory");
#else
#include <winsock2.h>
#include <windows.h>
#include <intrin.h>
#define tb_mb() _ReadWriteBarrier()
#if defined(_WIN64) || ((_WIN32_WINNT >= 0x0502) && defined(_WINBASE_) && !defined(_MANAGED))
#define CAS(des, old_v, new_v) InterlockedCompareExchange((uint64_t *)des, new_v, old_v) == old_v
#else
#define CAS(des, old_v, new_v) _InterlockedCompareExchange((unsigned long *)des, new_v, old_v) == old_v
#endif
#endif

    template <typename T>
    class K4RingBuffer
    {
    public:
        static K4RingBuffer<T> *create_instance_on_heap(int len);
        bool init(int len);
        uint64_t push(T &data, bool is_single_p = false); // 将T push入队列，is_single_p用去区分写入场景是否是单线程或单进程写
        uint64_t push(T &&data, bool is_single_p = false); 
        uint64_t get_new(T *&data, bool is_single_p = false);
        uint64_t get_new(bool is_single_p = false);
        inline T *get_by_sequence(const uint64_t sequence);
        inline T *get_by_index(const uint64_t sequence);
        inline uint64_t get_cur_sequence() { return prod.tail; }
        // 添加 zero 方法
        void zero() {
            prod.head = 0;
            prod.tail = 0;
            for (int i = 0; i < ring_size; ++i) {
                ring[i] = T(); // 重置元素为默认值
            }
        }

    public:
        struct _prod
        {
            volatile uint64_t head;  // 生产者头指针
            char padding1[64 - sizeof(uint64_t)];  // 填充到64字节
            volatile uint64_t tail;  // 生产者尾指针
            char padding2[64 - sizeof(uint64_t)];  // 填充到64字节
            int64_t appending[5];
        } prod;

        int32_t ring_size;
        int32_t index_mode;
        T ring[0];
    };

    template <typename T>
    K4RingBuffer<T> *K4RingBuffer<T>::create_instance_on_heap(int len)
    {
        if (len <= 0)
            return nullptr;
        K4RingBuffer<T> *res = (K4RingBuffer<T> *)malloc(sizeof(K4RingBuffer<T>) + sizeof(T) * len);
        res->init(len);
        return res;
    }

    template <typename T>
    bool K4RingBuffer<T>::init(int len)
    {
        if (len <= 1)
            return false;

        int res = len & (len - 1);
        if (res == 0)
            index_mode = 0;
        else
            index_mode = 1;

        this->ring_size = len;
        this->prod.head = 0;
        this->prod.tail = 0;

        return true;
    }

    template <typename T>
    uint64_t K4RingBuffer<T>::push(T &data, bool is_single_p)
    {
        uint64_t prod_head, prod_next, prod_indx;
        if (false == is_single_p)
        {
            bool success = false;
            do
            {
                prod_head = prod.head;
                prod_next = prod_head + 1;

                if (prod_next == prod.tail)
                    continue;

                success = CAS(&(prod.head), prod_head, prod_next);
            } while (success == false);
        }
        else
        {
            prod_next = ++prod.head;
        }
        // copy
        if (index_mode == 0)
            prod_indx = prod_next & (ring_size - 1);
        else
            prod_indx = prod_next % (ring_size);

        this->ring[prod_indx] = data;
        tb_mb();
        if (!is_single_p)
        {
            while (prod.tail != prod_head)
                ;
        }
        this->prod.tail = prod_next;
        return prod_indx;
    }

    template <typename T>
    uint64_t K4RingBuffer<T>::push(T &&data, bool is_single_p)
    {
        uint64_t prod_head, prod_next, prod_indx;
        if (!is_single_p)
        {
            bool success = false;
            do
            {
                prod_head = prod.head;
                prod_next = prod_head + 1;

                if (prod_next == prod.tail)
                    continue; // 缓冲区满，等待（或根据需求处理）

                success = CAS(&(prod.head), prod_head, prod_next);
            } while (!success);
        }
        else
        {
            prod_next = ++prod.head;
        }

        // 计算索引（与左值版本逻辑一致）
        if (index_mode == 0)
            prod_indx = prod_next & (ring_size - 1);
        else
            prod_indx = prod_next % ring_size;

        // 关键：使用移动赋值，而非拷贝赋值
        this->ring[prod_indx] = std::move(data); 

        tb_mb(); // 内存屏障，确保数据写入完成后再更新tail

        if (!is_single_p)
        {
            while (prod.tail != prod_head)
                ; // 等待其他生产者完成
        }
        this->prod.tail = prod_next;

        return prod_indx;
    }

    template <typename T>
    uint64_t K4RingBuffer<T>::get_new(T *&data, bool is_single_p)
    {
        uint64_t prod_head, prod_next, prod_indx;
        if (false == is_single_p)
        {
            bool success = false;
            do
            {
                prod_head = prod.head;
                prod_next = prod_head + 1;

                if (prod_next == prod.tail)
                    continue;

                success = CAS(&(prod.head), prod_head, prod_next);
            } while (success == false);
        }
        else
        {
            prod_next = ++prod.head;
        }
        // copy
        if (index_mode == 0)
            prod_indx = prod_next & (ring_size - 1);
        else
            prod_indx = prod_next % (ring_size);

        data = &(ring[prod_indx]);
        tb_mb();
        if (!is_single_p)
        {
            while (prod.tail != prod_head)
                ;
        }
        this->prod.tail = prod_next;
        return prod_indx;
    }

    template <typename T>
    uint64_t K4RingBuffer<T>::get_new(bool is_single_p)
    {
        uint64_t prod_head, prod_next, prod_indx;
        if (false == is_single_p)
        {
            bool success = false;
            do
            {
                prod_head = prod.head;
                prod_next = prod_head + 1;

                if (prod_next == prod.tail)
                    continue;

                success = CAS(&(prod.head), prod_head, prod_next);
            } while (success == false);
        }
        else
        {
            prod_next = ++prod.head;
        }
        // copy
        if (index_mode == 0)
            prod_indx = prod_next & (ring_size - 1);
        else
            prod_indx = prod_next % (ring_size);
        tb_mb();
        if (!is_single_p)
        {
            while (prod.tail != prod_head)
                ;
        }
        this->prod.tail = prod_next;
        return prod_indx;
    }

    template <typename T>
    T *K4RingBuffer<T>::get_by_sequence(const uint64_t sequence)
    {
        if (index_mode == 0)
            return &ring[sequence & (ring_size - 1)];
        else
            return &ring[sequence % (ring_size)];
    }

    template <typename T>
    T *K4RingBuffer<T>::get_by_index(const uint64_t sequence)
    {
        if (sequence >= ring_size)
            return nullptr;
        return &ring[sequence];
    }
};