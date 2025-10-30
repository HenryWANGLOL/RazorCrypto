#pragma once

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <cstdint>
#include <boost/static_assert.hpp>
#include <x86intrin.h> 
#define COMPILER_BARRIER() asm volatile("" ::: "memory")
inline pid_t gettid() {
    return (pid_t)syscall(SYS_gettid);
}

namespace shm {

    inline int32_t AAF(int32_t &var, int32_t value) {
        return __sync_add_and_fetch(&var, value);
    }

    inline int32_t FAA(volatile int32_t &var, int32_t value) {
        return __sync_fetch_and_add(&var, value);
    }

    template <typename T>
    inline T &interpret_as(void *data) {
        return *(T *)data;
    }

    template <typename T>
    inline const T &interpret_as(const void *data) {
        return *(const T *)data;
    }

    class shared_memory_queue {
#pragma pack(push)
#pragma pack(1)
        struct item_t {
            int len;
            char buf[16 + 128];
        };

        struct shared_data {
            int magic_number;
            int queue_size;
            // 添加缓存行填充
            char _pad1[64 - sizeof(int)*2]; 
            alignas(64) volatile int head; // 独占缓存行
            alignas(64) volatile int tail; // 独占缓存行
            char _pad2[64 - sizeof(int)];

            item_t items[1];
        };
#pragma pack(pop)

        enum {
            MAGIC_NUMBER = 0x38335687
        };

    public:
        shared_memory_queue(const char* name, int size)
            : _size((int)get_nextpowerof2(size)) {
            assert((_size & (_size - 1)) == 0 && "_size必须为2的幂");
            _mask = _size - 1;

            int shared_memory_size = sizeof(shared_data) + (sizeof(item_t) * (_size - 1));

            // 创建或打开共享内存
            int flags = O_RDWR | O_CREAT | O_EXCL;
            _shm_fd = shm_open(name, flags, 0666);
            if (_shm_fd == -1) {
                if (errno == EEXIST) {
                    // 已存在，直接打开
                    _shm_fd = shm_open(name, O_RDWR, 0666);
                    _is_creator = false;
                } else {
                    perror("shm_open失败");
                    exit(1);
                }
            } else {
                // 新创建，设置大小并初始化
                if (ftruncate(_shm_fd, shared_memory_size) == -1) {
                    perror("ftruncate失败");
                    exit(1);
                }
                _is_creator = true;
            }

            // 映射共享内存
            _shmptr = (char*)mmap(NULL, shared_memory_size, PROT_READ | PROT_WRITE, MAP_SHARED, _shm_fd, 0);
            if (_shmptr == MAP_FAILED) {
                perror("mmap失败");
                exit(1);
            }

            _data = reinterpret_cast<shared_data*>(_shmptr);

        if (_is_creator) {
            _data->magic_number = MAGIC_NUMBER;
            _data->queue_size = _size;
            _data->tail = 0;
            _data->head = 0; // 初始化head为0
        } else {
            while (_data->magic_number != MAGIC_NUMBER) usleep(1000);
            // 无需初始化_head，直接使用共享的head
        }
            _tail = _data->tail;
        }

        ~shared_memory_queue() {
            // 解除映射并关闭描述符
            munmap(_shmptr, sizeof(shared_data) + (sizeof(item_t) * (_size - 1)));
            close(_shm_fd);
        }

        // 其他成员函数保持原样...
        // push(), pop(), wait_and_pop()等实现不变

        template <typename T>
        void wait_and_pop(T& obj) {
            while (true) {
                int cur_head = __atomic_load_n(&_data->head, __ATOMIC_ACQUIRE);
                int cur_tail = __atomic_load_n(&_data->tail, __ATOMIC_ACQUIRE);
                
                if (cur_head < cur_tail) {
                    // 尝试原子地推进head
                    if (__atomic_compare_exchange_n(&_data->head, &cur_head, cur_head + 1,
                                                    false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
                        // 读取数据
                        memcpy(&obj, _data->items[cur_head & _mask].buf, sizeof(T));
                        break;
                    }
                } else {
                    _mm_pause(); // 减轻忙等待压力
                }
            }
        }
        void push(const void *buf, int len) {
            int cur_head = __atomic_load_n(&_data->head, __ATOMIC_ACQUIRE);
            int cur_tail = __atomic_load_n(&_data->tail, __ATOMIC_RELAXED);
            
            if (cur_tail - cur_head >= _size) {
                // 处理队列满的情况（如等待或返回错误）
                // 示例：usleep(1000); 短暂等待后重试
                return;
            }
            
            int cur = cur_tail & _mask;
            _data->items[cur].len = len;
            memcpy(_data->items[cur].buf, buf, len);
            
            // 确保写入完成后更新tail
            __atomic_store_n(&_data->tail, cur_tail + 1, __ATOMIC_RELEASE);
        }

        template <typename T>
        bool try_pop(T &obj) {
            if (_head >= _data->tail)
                return false;
            __sync_synchronize();
            if (_data->tail - _head >= _size) {
                printf("共享内存队列溢出\n");
            }
            int cur = FAA(_head, 1) & _mask;
            memcpy(&obj, _data->items[cur].buf, sizeof(T));
            return true;
        }



    private:
        uint64_t get_nextpowerof2(uint64_t n) {
            if (n & (n - 1)) {
                n |= n >> 1; n |= n >> 2;
                n |= n >> 4; n |= n >> 8;
                n |= n >> 16; n |= n >> 32;
                return ++n;
            }
            return n;
        }

        shared_data* _data;
        char* _shmptr;
        int _shm_fd;
        bool _is_creator;
        int _size;
        int _mask;
        int _head;
        int _tail;
    };
} // namespace shm