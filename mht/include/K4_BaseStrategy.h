/*****************************************************************************
所有crypto策略的基类
Created by Yanglei
20250708
*****************************************************************************/
#pragma once

#include "RT_ShmMdPlus.h"
#include "RT_TradeDataStruct.h"
#include "RT_MQ.h"
#include "K4_TradeItem.h"
#include <yaml-cpp/yaml.h>
#include <thread>
using namespace mht_rt;

class K4BaseStrategy {
public:
    K4BaseStrategy(YAML::Node &config) {
    }

    virtual ~K4BaseStrategy() {}



    shm_md_plus<RT_FuturesSnapshot>::md_head_plus* sub_depth10(std::string dev_shm_file,std::string symbol){
        auto shm = md_manager<RT_FuturesSnapshot>::get_instance()->get_or_create_shm(dev_shm_file);
        shm_md_plus<RT_FuturesSnapshot>::md_head_plus* hdr = shm->sub_md(symbol.data());
        if(hdr != nullptr)
            m_depth10[hdr] = hdr->current_index;
        return hdr;
    }

    void poll_depth10(){
        for(auto &it:m_depth10){
            if(it.first->current_index!=it.second){
                it.second = it.first->current_index;
                RT_FuturesSnapshot *data = it.first->get_cur();
            }
        }
    }



    int work() {
        while(m_running){
            //std::this_thread::sleep_for(std::chrono::seconds(1));
            poll_depth10();
        }
        return 0;
    }

    void start_work(){
        m_work = std::thread([this]() { this->work(); });
    }

    
public:
     bool m_running = true;

private:
    std::thread m_work;
    ska::flat_hash_map<shm_md_plus<RT_FuturesSnapshot>::md_head_plus*,uint64_t> m_depth10;
};