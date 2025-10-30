#pragma once
//
// Created by YangLei on 2024-01-23.
//

#include <vector>
#include <unordered_set>
#include <cstdio>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <iostream>
#include "RT_ShmUtil.h"

#ifdef __linux__
#include <sys/mman.h>
#include <sys/types.h>
#include "flat_hash_map.hpp"
#define mb() asm volatile("mfence" : : : "memory")
#else
#include <unordered_map>
#define mb()    
#define compile_fence()
#endif

#define SECURITY_MAX_LENS 16
using namespace mht_rt;

struct md_shm_head {
	uint64_t update_num;
	uint32_t status;
	uint32_t md_num;
};

struct md_head {
	uint64_t update_time;
	volatile int64_t current_index;
	char instrument_id[32] = { 0 };

	template <class market_data>
	market_data* get_cur_md(){
		market_data* base = (market_data*)(this + 1);
		int offset = this->current_index % 3;
		if (offset < 0) offset += 3;  // 修正负模
		return base + offset;
	}
};

class md_shm_util{
public:
	static uint64_t com_hash(const char* security_id, unsigned int len){
		uint64_t ret = 0;
		unsigned i;
		unsigned int seed = 131; // the magic number, 31, 131, 1313, 13131, etc.. orz.
		for (i = 0; i < len; ++i){
			ret = ret * seed + security_id[i];
		}
		return ret;
	}
};

template <class market_data>
class shm_md_tick
{
public:
	shm_md_tick() {
		m_md_num = 0;
		m_max_md_num = 0;
		m_shm_head = nullptr;
		m_md_dump_fun = nullptr;
	}

	~shm_md_tick() {
		share_memory_util::get_instance()->release_page_buffer((void*)m_shm_head, m_shm_size, true);
	}

	void* create_shm_ptr(std::string shm_name, int md_num) {
		if (m_shm_head != nullptr)
			return m_shm_head;

		m_shm_size = md_num * (sizeof(md_head) + sizeof(market_data) * MD_BLOCK_NUM)
			+ sizeof(md_shm_head);
		this->m_max_md_num = md_num;

#ifdef __linux__
		std::string md_fn = "/dev/shm/" + shm_name;
		remove(md_fn.data());
#endif
		void* ptr = share_memory_util::get_instance()->load_page_buffer(shm_name, m_shm_size, true, true, true);
		memset(ptr, 0, m_shm_size);
		return ptr;
	}

	void init_shm(std::vector<std::string>& instruments) {
		for (unsigned i = 0; i < instruments.size(); ++i) {
			init_instrument_shm(m_next_empty_md, instruments[i]);
			m_next_empty_md = (md_head*)((market_data*)(m_next_empty_md + 1) + MD_BLOCK_NUM);
		}
		int left = m_max_md_num - instruments.size();
		md_head* tail = m_next_empty_md;
		for (int i = 0; i < left; ++i) {
			tail->current_index = -1;
			tail = get_next_md_head(tail);

		}
	}

	inline void init_instrument_shm(md_head* head, std::string& instrument) {
		uint64_t security_no = md_shm_util::com_hash(instrument.data(), instrument.size());
		m_instruments_address[security_no] = head;
		memset(head, 0, sizeof(md_head) + sizeof(market_data) * MD_BLOCK_NUM);
		strncpy(head->instrument_id, instrument.c_str(), sizeof(head->instrument_id) - 1);
		m_shm_head->md_num++;
	}

	inline md_head* get_next_md_head(md_head* mh) {
		return (md_head*)((market_data*)(mh + 1) + MD_BLOCK_NUM);
	}

	void set_shm_addr(md_shm_head* p, std::vector<std::string>& instruments, bool write_pid = false) {
		m_shm_head = p;
		m_md_head = m_next_empty_md = (md_head*)(p + 1);
		if (write_pid) {
			m_shm_head->md_num = 0;
			m_md_num += (int)(instruments.size());
			init_shm(instruments);
			mb();
			m_shm_head->status = m_shm_head->update_num = 0;
		}
		else {
			mb();
		}
	}

	md_shm_head* get_shm(std::string _shm_name, size_t& _shm_size) {
		struct stat info;
#ifdef __linux__
		std::string shm_name = "/dev/shm/" + _shm_name;
#else
		std::string shm_name = _shm_name;
#endif
		bool is_exists = share_memory_util::get_instance()->file_exists(shm_name);
		if(!is_exists){
			std::cout<<"shm file is not exists:"<<shm_name<<std::endl;
			exit(0);
		}
		stat(shm_name.data(), &info);
		_shm_size = info.st_size;

		void* ret = share_memory_util::get_instance()->load_page_buffer(_shm_name, _shm_size, false, true, true,false);
		this->m_shm_size = info.st_size;

		m_shm_head = (md_shm_head*)ret;
		m_md_head = m_next_empty_md = (md_head*)(m_shm_head + 1);
		while (m_next_empty_md->current_index >= 0)
			m_next_empty_md = get_next_md_head(m_next_empty_md);
		load_instruments_addresses();

		return (md_shm_head*)ret;
	}

	void load_instruments_addresses() {
		//market_data* data;
		std::string instrument;
		for (md_head* index = m_md_head; index < m_next_empty_md; index = get_next_md_head(index)) {
			//data = (market_data*)(index + 1);
			instrument = std::string(index->instrument_id);
			m_instruments_address[md_shm_util::com_hash(instrument.data(), instrument.length())] = index;
		}
	}

#ifdef __linux__
	inline ska::flat_hash_map<uint64_t, md_head*>* get_instruments_address() {
		return &m_instruments_address;
	}
#else
	inline std::unordered_map<uint64_t, md_head*>* get_instruments_address() {
		return &m_instruments_address;
	}
#endif

	bool Is_updated(market_data* p) {
		if (m_shm_head == nullptr)
			return true;
		uint64_t security_no = md_shm_util::com_hash(p->instrument_id, strlen(p->instrument_id));
		if (security_no == 0) return false;
		auto iter = m_instruments_address.find(security_no);
		if (iter == m_instruments_address.end())
			return true;

		md_head* index = iter->second;
		if (index->current_index == 0) return false;
		market_data* old_current = (market_data*)(index + 1) + index->current_index%MD_BLOCK_NUM;
		auto left = p->total_trade_volume - old_current->total_trade_volume;
		return left > 10;

	}

	inline md_shm_head* get_shm_head_addr() {
		return m_shm_head;
	}

	inline int get_shm_size() {
		return m_shm_size;
	}

	inline md_head* get_first_md_head() {
		return m_md_head;
	}

	market_data* get_active_md(void* p, uint64_t& md_update_num) {
		md_head* mdHead = (md_head*)p;
		md_update_num = mdHead->current_index;
		market_data* ret = get_cur_md(mdHead);
		return ret;
	}

	inline bool is_valid_md(void* p) {
		return ((md_head*)p)->current_index >= 0;
	}

	market_data* get_cur_md(md_head* md_head) {
		market_data* ret = (market_data*)(md_head + 1);
		int offset = md_head->current_index % MD_BLOCK_NUM;
		return ret + offset;
	}

	market_data* get_cur_md(md_head* md_head, uint64_t& md_update_num) {
		md_update_num = md_head->current_index;
		market_data* ret = (market_data*)(md_head + 1);
		int offset = md_head->current_index % MD_BLOCK_NUM;
		return ret + offset;
	}

	market_data* get_cur_md(const std::string& instrument, uint64_t& md_update_num) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument.data(), instrument.length()));
		if (iter != m_instruments_address.end()) {
			return get_cur_md(iter->second, md_update_num);
		}
		return nullptr;
	}

	market_data* get_cur_md(const std::string& instrument) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument.data(), instrument.length()));
		if (iter != m_instruments_address.end()) {
			return get_cur_md(iter->second);
		}
		return nullptr;
	}

	market_data* get_cur_md(const char* instrument,int len, uint64_t& md_update_num) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument, len));
		if (iter != m_instruments_address.end()) {
			return get_cur_md(iter->second, md_update_num);
		}
		return nullptr;
	}

	market_data* get_cur_md(const char* instrument, int len) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument, len));
		if (iter != m_instruments_address.end()) {
			return get_cur_md(iter->second);
		}
		return nullptr;
	}

	uint64_t get_update_num(const std::string& instrument) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument.data(), instrument.length()));
		if (iter != m_instruments_address.end()) {
			return iter->second->current_index;
		}
		return 0;
	}

	bool on_rtn_data(char* instrumentID, market_data& newData, std::function<bool(market_data* old_md, market_data* new_md)> compare = nullptr) {
		if (m_shm_head == nullptr)
			return false;

		uint64_t security_no = md_shm_util::com_hash(instrumentID, strlen(instrumentID));
		auto iter = m_instruments_address.find(security_no);
		if (iter == m_instruments_address.end()){
			if (m_md_num < m_max_md_num) {
				std::string id(instrumentID);
				init_instrument_shm(m_next_empty_md, id);
				m_next_empty_md = (md_head*)((market_data*)(m_next_empty_md + 1) + MD_BLOCK_NUM);
				iter = m_instruments_address.find(security_no);
				++m_md_num;
			}
			else {
				return false;
			}
		}
		md_head* index = iter->second;
		bool updated = true;
		if (compare != nullptr) {
			market_data* old_current = (market_data*)(index + 1) + index->current_index % MD_BLOCK_NUM;
			updated = compare(old_current, &newData);
		}
		if (updated) {
			int offset = (index->current_index + 1) % MD_BLOCK_NUM;
			market_data* data = (market_data*)(index + 1) + offset;
			if (m_md_dump_fun != nullptr)
				m_md_dump_fun(data, newData);
			else
				*data = newData;
			mb();
			++index->current_index;
			++m_shm_head->update_num;
			return true;
		}
		return false;
	}

	md_head* sub_md(const char* instrumentID) {
		if (m_shm_head == nullptr)
			return nullptr;

		uint64_t security_no = md_shm_util::com_hash(instrumentID, strlen(instrumentID));
		auto iter = m_instruments_address.find(security_no);
		if (iter == m_instruments_address.end()){
			return nullptr;
		}
		md_head* index = iter->second;
		return index;
	}

	void set_md_dump_func(std::function<void(market_data*, market_data&)> fun){
		m_md_dump_fun = fun;
	}

private:
	md_shm_head* m_shm_head;
	md_head* m_md_head, * m_next_empty_md;
	int m_max_md_num = 0;
	int m_md_num = 0; 
	int m_shm_size = 0; 
	int MD_BLOCK_NUM = 3;

#ifdef __linux__
	ska::flat_hash_map<uint64_t, md_head*> m_instruments_address;
#else
	std::unordered_map<uint64_t, md_head*> m_instruments_address;
#endif
	std::function<void(market_data*, market_data&)> m_md_dump_fun = nullptr;
};