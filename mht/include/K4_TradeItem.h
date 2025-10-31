#pragma once
#include "RT_ShmMdPlus.h"
#include "RT_TradeDataStruct.h"
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>
using namespace mht_rt;


template<typename T>
class md_manager {
public:
    static md_manager* get_instance() {
        static md_manager res;
        return &res;
    }

    shm_md_plus<T>* get_or_create_shm(const std::string& md_path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_con.find(md_path);
        if (it != m_con.end())
            return it->second.get();

        auto md_reader = std::make_unique<shm_md_plus<T>>();
        size_t sz;
        md_reader->get_shm(md_path, sz, true);
        shm_md_plus<T>* ptr = md_reader.get();
        m_con[md_path] = std::move(md_reader);
        return ptr;
    }

    shm_md_plus<T>* get_from_path(const std::string& md_path) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_con.find(md_path);
        return (it != m_con.end()) ? it->second.get() : nullptr;
    }

private:
    md_manager() = default;
    ~md_manager() = default;

    md_manager(const md_manager&) = delete;
    md_manager& operator=(const md_manager&) = delete;

    std::mutex m_mutex;
    std::unordered_map<std::string, std::unique_ptr<shm_md_plus<T>>> m_con;
};


typedef struct _stratgy_trade{
    char instrument_id[32] = { 0 };
    double volume = 0.0f;
    double px = 0.0f;
    int64_t order_trade_id;//成交id
    uint32_t stg_order_id = 0;//策略单号
    uint32_t stg_id = 0;//策略的源ID，标识这个下单req来自何处
    uint32_t td_id = 0;//td_id,标识这个req要发往何处
}stratgy_trade;
