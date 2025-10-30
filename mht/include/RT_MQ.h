#pragma once
#include "RT_RingBuffer.h"
#include "RT_ShmUtil.h"
#include <cxxabi.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string>
#include <iostream>
#include <memory>
#include <vector>
#include <system_error>

namespace mht_rt
{

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096 // 大多数系统的页面大小为4KB
#endif

    constexpr size_t PUBSUBLEN = 1024 * 512;

    inline size_t align_to_page(size_t size)
    {
        return (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    }

    // 获取类型的可读名称
    inline std::string getDemangledTypeName(const std::type_info &typeInfo)
    {
        int status;
        char *demangled_name = abi::__cxa_demangle(typeInfo.name(), nullptr, nullptr, &status);
        std::string result = (status == 0) ? demangled_name : typeInfo.name();
        std::free(demangled_name);
        return result;
    }

    // 共享内存管理辅助类
    class SharedMemory
    {
    public:
        SharedMemory(const std::string &name, size_t size, bool &create, bool read_only)
            : m_name(name), m_size(size)
        {
            open_or_create(create, read_only);
        }

        ~SharedMemory()
        {
            close();
        }

        void *get_address() const { return m_address; }
        size_t get_size() const { return m_size; }

    private:
        std::string m_name;
        size_t m_size;
        int m_fd = -1;
        void *m_address = nullptr;

        void open_or_create(bool &create, bool read_only)
        {
            if (create)
            {
                // 如果create为true，先删除已存在的共享内存
                shm_unlink(m_name.c_str());
            }
            else
            {
                // 如果create为false但文件不存在，创建新的共享内存
                struct stat info;
                if (stat(("/dev/shm" + m_name).c_str(), &info) != 0)
                {
                    create = true;
                }
            }

            const int flags = create ? (O_CREAT | O_RDWR) : (read_only ? O_RDONLY : O_RDWR);
            printf("open name =%s ", m_name.c_str());
            m_fd = shm_open(m_name.c_str(), flags, 0777);
            if (m_fd == -1)
            {
                throw std::system_error(errno, std::generic_category(), "shm_open failed");
            }

            if (create)
            {
                if (ftruncate(m_fd, static_cast<off_t>(m_size)) == -1)
                {
                    close();
                    throw std::system_error(errno, std::generic_category(), "ftruncate failed");
                }
            }

            const int prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
            m_address = mmap(nullptr, m_size, prot, MAP_SHARED, m_fd, 0);
            if (m_address == MAP_FAILED)
            {
                close();
                throw std::system_error(errno, std::generic_category(), "mmap failed");
            }

        }

        void close()
        {
            if (m_address != MAP_FAILED && m_address != nullptr)
            {
                munmap(m_address, m_size);
            }
            if (m_fd != -1)
            {
                ::close(m_fd);
            }
        }
    };

    template <class T>
    class MQ_Pub
    {
    public:
        MQ_Pub(size_t capacity = PUBSUBLEN, bool is_creator = false)
            : m_capacity(validate_capacity(capacity)),
              create(is_creator)
        {
            initialize(create);
        }

        ~MQ_Pub() = default;

        // 禁用拷贝，允许移动
        MQ_Pub(const MQ_Pub &) = delete;
        MQ_Pub &operator=(const MQ_Pub &) = delete;
        MQ_Pub(MQ_Pub &&) = default;
        MQ_Pub &operator=(MQ_Pub &&) = default;

        uint64_t push(T &data, bool is_single_process = false)
        {
            return m_ring_buffer->push(data, is_single_process);
        }

        uint64_t push(T &&data, bool is_single_process = false) {
            return m_ring_buffer->push(std::forward<T>(data), is_single_process); 
        }

        uint64_t get_shm_size() const { return m_shared_memory->get_size(); }
        size_t get_capacity() const { return m_capacity; }

        T *get_by_id(uint64_t id) const
        {
            return m_ring_buffer->get_by_sequence(id);
        }

        // 添加 zero 方法
        void zero() {
            m_ring_buffer->zero();
        }

    private:
        std::unique_ptr<SharedMemory> m_shared_memory;
        K4RingBuffer<T> *m_ring_buffer = nullptr;
        size_t m_capacity;
        bool create;

        size_t validate_capacity(size_t capacity) const
        {
            if (capacity < 16) capacity = 16;
            capacity--;
            capacity |= capacity >> 1;
            capacity |= capacity >> 2;
            capacity |= capacity >> 4;
            capacity |= capacity >> 8;
            capacity |= capacity >> 16;
            capacity++;
            return capacity;
        }

        void initialize(bool is_creator)
        {
            const std::string type_name = getDemangledTypeName(typeid(T));
            const std::string shm_name = "/shm_mq_" + type_name;
            const size_t buffer_size = calculate_buffer_size();

            try
            {
                m_shared_memory = std::make_unique<SharedMemory>(shm_name, buffer_size, is_creator, false);
                m_ring_buffer = reinterpret_cast<K4RingBuffer<T> *>(m_shared_memory->get_address());

                if (is_creator)
                {
                    m_ring_buffer->init(m_capacity);
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "MQ_Pub initialization failed: " << e.what() << std::endl;
                throw;
            }
        }

        size_t calculate_buffer_size() const
        {
            const size_t min_size = sizeof(K4RingBuffer<T>) + sizeof(T) * m_capacity;
            return align_to_page(min_size);
        }
    };

    template <class T>
    class MQ_Sub
    {
    public:
        MQ_Sub(size_t capacity = PUBSUBLEN, bool require_existing = false, bool start_from_beginning = false)
            : m_capacity(validate_capacity(capacity))
        {
            initialize(require_existing, start_from_beginning);
            std::cout << "[MQSUB inited] Starting index: " << m_current_index << std::endl;
        }

        ~MQ_Sub() = default;

        // 禁用拷贝，允许移动
        MQ_Sub(const MQ_Sub &) = delete;
        MQ_Sub &operator=(const MQ_Sub &) = delete;
        MQ_Sub(MQ_Sub &&) = default;
        MQ_Sub &operator=(MQ_Sub &&) = default;

        bool try_read_new(T *&data)
        {
            const uint64_t current = m_ring_buffer->get_cur_sequence();
            if (m_current_index < current)
            {
                data = m_ring_buffer->get_by_sequence(++m_current_index);
                return data != nullptr;
            }
            return false;
        }

        const T *get_by_id(uint64_t id) const
        {
            return m_ring_buffer->get_by_sequence(id);
        }

        const T *get_current() const
        {
            const uint64_t current = m_ring_buffer->get_cur_sequence();
            return get_by_id(current);
        }

        uint64_t get_shm_size() const { return m_shared_memory->get_size(); }
        size_t get_capacity() const { return m_capacity; }
        uint64_t get_current_index() const { return m_current_index; }

        // 获取所有待处理的更新
        std::vector<T> get_all_pending()
        {
            const uint64_t current = m_ring_buffer->get_cur_sequence();
            if (m_current_index >= current)
            {
                return {};
            }

            const uint64_t max_history = std::min(current - m_current_index, static_cast<uint64_t>(m_capacity));
            std::vector<T> result;
            result.reserve(max_history);

            for (uint64_t i = m_current_index + 1; i <= current; ++i)
            {
                const T *data = m_ring_buffer->get_by_sequence(i);
                if (data)
                {
                    result.push_back(*data);
                }
            }

            m_current_index = current;
            return result;
        }

        // 获取所有数据
        std::vector<T> get_all() {
            const uint64_t current = m_ring_buffer->get_cur_sequence();
            std::vector<T> result;
            for (uint64_t i = 0; i <= current; ++i) {
                const T* data = m_ring_buffer->get_by_sequence(i);
                if (data) {
                    result.push_back(*data);
                }
            }
            return result;
        }

        // 重置到起始位置，从0开始读取
        void reset_to_beginning() {
            m_current_index = 0;
        }

        // 跳转到指定索引位置，从该位置开始读取
        // 返回值：true表示跳转成功，false表示索引无效
        bool skipMessage(uint64_t index) {
            const uint64_t current_sequence = m_ring_buffer->get_cur_sequence();
            
            // 检查索引是否有效
            if (index > current_sequence) {
                std::cerr << "Warning: Skip index " << index << " is beyond current sequence " << current_sequence << std::endl;
                return false;
            }
            
            m_current_index = index;
            return true;
        }

        // 添加 zero 方法
        void zero() {
            m_ring_buffer->zero();
            m_current_index = 0;
        }

    private:
        std::unique_ptr<SharedMemory> m_shared_memory;
        K4RingBuffer<T> *m_ring_buffer = nullptr;
        size_t m_capacity;
        uint64_t m_current_index = 0;

        size_t validate_capacity(size_t capacity) const
        {
            if (capacity < 16)
            {
                std::cerr << "Warning: Capacity too small, setting to 16" << std::endl;
                return 16;
            }
            return capacity;
        }

        void initialize(bool require_existing, bool start_from_beginning)
        {
            const std::string type_name = getDemangledTypeName(typeid(T));
            const std::string shm_name = "/shm_mq_" + type_name;

            try
            {
                // 总是使用第一个参数作为大小
                const size_t buffer_size = calculate_buffer_size();

                // 处理require_existing标志
                if (require_existing)
                {
                    struct stat info;
                    if (stat(("/dev/shm" + shm_name).c_str(), &info) != 0)
                    {
                        throw std::runtime_error("Shared memory does not exist and require_existing is true");
                    }
                }

                // 创建共享内存，create标志由SharedMemory类内部逻辑决定
                bool create = false;
                m_shared_memory = std::make_unique<SharedMemory>(shm_name, buffer_size, create, false);
                m_ring_buffer = reinterpret_cast<K4RingBuffer<T> *>(m_shared_memory->get_address());

                // 检查是否需要初始化环形缓冲区
                if (is_shared_memory_initialized())
                {
                    // 根据标志决定是从0开始还是从当前序列开始
                    m_current_index = start_from_beginning ? 0 : m_ring_buffer->get_cur_sequence();
                }
                else
                {
                    m_ring_buffer->init(m_capacity);
                    m_current_index = 0;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "MQ_Sub initialization failed: " << e.what() << std::endl;
                throw;
            }
        }

        // 检查共享内存是否已经初始化
        bool is_shared_memory_initialized() const
        {
            // 简单的检查：如果头指针的序列号不为0，则认为已经初始化
            // 这依赖于K4RingBuffer的实现细节
            return m_ring_buffer->get_cur_sequence() != 0;
        }

        size_t calculate_buffer_size() const
        {
            const size_t min_size = sizeof(K4RingBuffer<T>) + sizeof(T) * m_capacity;
            return align_to_page(min_size);
        }
    };
};