#pragma once
//
// Created by YangLei on 2024-07-02.
//

#include <vector>
#include <cstdio>
#include <ctime>
#include <string>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <functional>
#include <iostream>
#include "RT_ShmUtil.h"
#include "RT_ShmMdTick.h"
#include "RT_Common.h"

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

struct md_shm_head_plus {
	uint64_t update_num;
	uint32_t md_num;
	uint32_t md_buff_num;
};

template <class market_data>
class shm_md_plus
{
public:
	struct md_head_plus {
		uint64_t update_time;
		volatile int64_t current_index;
		char instrument_id[16] = { 0 };
		int buffer_len = 0;
		market_data dat[0];

		market_data* get_cur(){ 
			if(current_index>=1){
				return &dat[(current_index-1)%buffer_len];
			}
			return nullptr;
		}
	};

	struct md_ret {
		bool ret = false;
		uint32_t buff_len = 0;
		uint32_t start_nTime = 0;
		uint32_t end_nTime = 0;
		uint64_t start_index = 0;
		uint64_t end_index = 0;
		market_data* dat = nullptr;
	};

	shm_md_plus() {
		m_md_num = 0;
		m_max_instrument_num = 0;
		m_shm_head = nullptr;
		m_md_dump_fun = nullptr;
	}

	~shm_md_plus() {
		share_memory_util::get_instance()->release_page_buffer((void*)m_shm_head, m_shm_size, true);
	}

	void* create_shm_ptr(std::string shm_name, uint32_t max_instrument_num, uint32_t md_buff_num, bool is_shm_open) {
		if (m_shm_head != nullptr)
			return m_shm_head;

		MD_BLOCK_NUM = md_buff_num;
		m_shm_size = max_instrument_num * (sizeof(md_head_plus) + sizeof(market_data) * MD_BLOCK_NUM)
			+ sizeof(md_shm_head_plus);
		this->m_max_instrument_num = max_instrument_num;

		/*int pagesize = getpagesize();
		if (m_shm_size % pagesize != 0) {
			auto tmp = m_shm_size;
			m_shm_size = m_shm_size - (m_shm_size%pagesize) + pagesize;
			std::cout<<"adjust shm size:"<<tmp<<" 2 "<<m_shm_size<<std::endl;
		}*/
#ifdef __linux__
		if (is_shm_open) {
			std::string md_fn = "/dev/shm/" + shm_name;
			remove(md_fn.data());
		}
		else {
			remove(shm_name.data());
		}
#endif
		void* ptr = share_memory_util::get_instance()->load_page_buffer(shm_name, m_shm_size, true, true, is_shm_open);
		if (ptr == nullptr) {
			std::cout << "create shm fail:" << shm_name << std::endl;
			exit(0);
		}
		memset(ptr, 0, m_shm_size);
		m_shm_head = (md_shm_head_plus*)ptr;
		m_shm_head->md_buff_num = md_buff_num;

		return ptr;
	}

	void init_shm(std::vector<std::string>& instruments) {
		for (unsigned i = 0; i < instruments.size(); ++i) {
			init_instrument_shm(m_next_empty_md, instruments[i]);
			m_next_empty_md = (md_head_plus*)((market_data*)(m_next_empty_md + 1) + MD_BLOCK_NUM);
		}
		int left = m_max_instrument_num - instruments.size();
		md_head_plus* tail = m_next_empty_md;
		for (int i = 0; i <= left; ++i) {
			tail->current_index = -1;
			tail = get_next_md_head(tail);

		}
	}

	inline void init_instrument_shm(md_head_plus* head, std::string& instrument) {
		m_instruments_address.insert(
			std::pair<long long, md_head_plus*>(md_shm_util::com_hash(instrument.data(), instrument.length()), head));
		// std::cout << "init hash " << com_hash(instrument.data(), instrument.length()) << " ins" << instrument << " len " << instrument.length() <<  std::endl;
		memset(head, 0, sizeof(md_head_plus) + sizeof(market_data) * MD_BLOCK_NUM);
		//market_data* data_field = &(head->dat[0]);
		strncpy(head->instrument_id, instrument.c_str(), sizeof(head->instrument_id) - 1);
		head->buffer_len = MD_BLOCK_NUM;
		m_shm_head->md_num++;
	}

	inline md_head_plus* get_next_md_head(md_head_plus* mh) {
		return (md_head_plus*)((market_data*)(mh + 1) + MD_BLOCK_NUM);
	}

	void set_shm_addr(md_shm_head_plus* p, std::vector<std::string>& instruments, bool write_pid = false) {
		m_shm_head = p;
		m_md_head = m_next_empty_md = (md_head_plus*)(p + 1);
		if (write_pid) {
			m_shm_head->md_num = 0;
			m_md_num += (int)(instruments.size());
			init_shm(instruments);
			mb();
			m_shm_head->update_num = 0;
		}
		else {
			mb();
		}
	}

	void re_init_shm(std::vector<std::string>& instruments) {
		int buff_len = m_shm_head->md_buff_num;

		memset((void*)m_shm_head, 0, m_shm_size);
		m_md_head = m_next_empty_md = (md_head_plus*)(m_shm_head + 1);
		m_shm_head->md_num = 0;
		m_md_num = (int)(instruments.size());
		init_shm(instruments);
		mb();
		m_shm_head->update_num = 0;
		m_shm_head->md_buff_num = buff_len;
	}

	md_shm_head_plus* get_shm(std::string _shm_name, size_t& _shm_size, bool is_shm_open) {
		struct stat info;
		std::string shm_name = "/dev/shm/" + _shm_name;
		if (false == is_shm_open)
			shm_name = _shm_name;

		bool is_exists = share_memory_util::get_instance()->file_exists(shm_name);
		if(!is_exists){
			std::cout<<"shm file is not exists:"<<shm_name<<std::endl;
			exit(0);
		}
		std::cout<<"shm file exists:"<<shm_name<<std::endl;
		stat(shm_name.data(), &info);
		_shm_size = info.st_size;

		void* ret = share_memory_util::get_instance()->load_page_buffer(_shm_name, _shm_size, false, true, is_shm_open);
		this->m_shm_size = info.st_size;

		m_shm_head = (md_shm_head_plus*)ret;
		MD_BLOCK_NUM = m_shm_head->md_buff_num;

		m_md_head = m_next_empty_md = (md_head_plus*)(m_shm_head + 1);
		while (m_next_empty_md->current_index >= 0) {
			m_next_empty_md = get_next_md_head(m_next_empty_md);
		}
		load_instruments_addresses();

		return (md_shm_head_plus*)ret;
	}

	void load_instruments_addresses() {
		std::string instrument;
		for (md_head_plus* index = m_md_head; index < m_next_empty_md; index = get_next_md_head(index)) {
			market_data* data = get_cur_md(index);
			instrument = std::string(index->instrument_id);
			m_instruments_address[md_shm_util::com_hash(instrument.data(), instrument.length())] = index;
		}
	}

	md_shm_head_plus* open_or_create(std::string _shm_name, std::vector<std::string> instruments, uint32_t md_buff_num, bool is_shm_open = true) {
        bool if_shm_exists = false;
		std::string shm_name;
#ifdef __linux__
		if (is_shm_open) {
			shm_name = "/dev/shm/" + _shm_name;
		}
		else {
			shm_name = _shm_name;
		}
#endif
        struct stat info;
        if (stat(shm_name.data(), &info) == 0) {  // 文件存在
            if_shm_exists = true;
        }
        if (!if_shm_exists) {
			void* ptr;
			ptr = create_shm_ptr(_shm_name, instruments.size(), md_buff_num, is_shm_open);
			// 设置共享内存地址
			set_shm_addr((md_shm_head_plus*)ptr, instruments, true);
			return (md_shm_head_plus*)ptr;
        }
        else {
			md_shm_head_plus* ptr;
            size_t shm_size;
            ptr = get_shm(_shm_name, shm_size, true);
			return ptr;
        }
		return nullptr;
	}

	inline md_shm_head_plus* get_shm_head_addr() {
		return m_shm_head;
	}

	inline int get_shm_size() {
		return m_shm_size;
	}

	inline md_head_plus* get_first_md_head() {
		return m_md_head;
	}

	market_data* get_active_md(void* p, uint64_t& md_update_num) {
		md_head_plus* mdHead = (md_head_plus*)p;
		md_update_num = mdHead->current_index;
		market_data* ret = get_cur_md(mdHead);
		return ret;
	}

	inline bool is_valid_md(void* p) {
		return ((md_head_plus*)p)->current_index >= 0;
	}

	uint64_t convert_to_epoch(const uint64_t& datetime,const long long offset) {
		// 分解时间戳：yyyymmdd hh mm ss mmm
		uint64_t year = datetime / 10000000000000ULL;
		uint64_t month = (datetime / 100000000000ULL) % 100;
		uint64_t day = (datetime / 1000000000ULL) % 100;
		uint64_t hour = (datetime / 10000000ULL) % 100;
		uint64_t minute = (datetime / 100000ULL) % 100;
		uint64_t second = (datetime / 1000ULL) % 100;
		uint64_t millisecond = datetime % 1000;

		// 转换为 tm 结构体
		struct tm tm = {0};
		tm.tm_year = static_cast<int>(year) - 1900;
		tm.tm_mon = static_cast<int>(month) - 1;
		tm.tm_mday = static_cast<int>(day);
		tm.tm_hour = static_cast<int>(hour);
		tm.tm_min = static_cast<int>(minute);
		tm.tm_sec = static_cast<int>(second);

		// 转换为 time_t 并应用偏移
		time_t time = mktime(&tm);
		if (time == -1) {
			throw std::runtime_error("Invalid datetime");
		}
		time -= offset;  // 向前偏移

		// 转换回 tm 结构体
		localtime_r(&time, &tm);

		// 处理毫秒部分
		int64_t new_millisecond = millisecond - (offset % 1000) * 1000;
		if (new_millisecond < 0) {
			new_millisecond += 1000;
			time -= 1;  // 借位 1 秒
			localtime_r(&time, &tm);
		}
		millisecond = static_cast<uint64_t>(new_millisecond);

		// 重新组合为 uint64_t
		uint64_t new_timestamp =
			(static_cast<uint64_t>(tm.tm_year + 1900) * 10000000000000ULL) +
			(static_cast<uint64_t>(tm.tm_mon + 1) * 100000000000ULL) +
			(static_cast<uint64_t>(tm.tm_mday) * 1000000000ULL) +
			(static_cast<uint64_t>(tm.tm_hour) * 10000000ULL) +
			(static_cast<uint64_t>(tm.tm_min) * 100000ULL) +
			(static_cast<uint64_t>(tm.tm_sec) * 1000ULL) +
			millisecond;

		return new_timestamp;
	}

	market_data* get_cur_md(md_head_plus* md_head_plus) {
		int offset = (md_head_plus->current_index - 1) % MD_BLOCK_NUM;
		if (offset < 0)
			offset = 0;
		return &md_head_plus->dat[offset];
	}

	market_data* get_cur_md(md_head_plus* md_head_plus, uint64_t& md_update_num) {
		md_update_num = md_head_plus->current_index;
		int offset = (md_head_plus->current_index - 1) % MD_BLOCK_NUM;
		if (offset < 0)
			offset = 0;
		return &md_head_plus->dat[offset];
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

	market_data* get_cur_md(const char* instrument, int len, uint64_t& md_update_num) {
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

	md_ret get_md_by_tp(const char* instrument, uint32_t len,uint64_t end_tp) {
		md_ret ret;
		ret.ret = false;
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument, len));
		if (iter != m_instruments_address.end()) {
			market_data* dat = iter->second->dat;
			uint64_t end = iter->second->current_index - 1;
			uint64_t start = (end + 1 >= MD_BLOCK_NUM) ? (end + 1 - MD_BLOCK_NUM) : 0;

			market_data* end_dat = &dat[end % MD_BLOCK_NUM];
			market_data* start_dat = &dat[start % MD_BLOCK_NUM];
			if (start_dat->NTimeMill> end_tp) {
				return ret;
			}

			ret.ret = true;
			ret.buff_len = MD_BLOCK_NUM;

			if (end_tp >= end_dat->NTimeMill) {
				ret.end_index = end;
				ret.end_nTime = end_dat->NTimeMill;
				ret.start_index = ret.end_index;
				ret.start_nTime = ret.end_nTime;
			}
			else {
				int64_t left = start, right = end;
				while (left < right) {
					auto mid = (left + right) / 2;
					auto tp = dat[mid % MD_BLOCK_NUM].NTimeMill;
					if (tp == end_tp) {
						right = mid;
						break;
					}
					if (tp > end_tp) {
						right = mid - 1;
						if (right < 0) {
							right = mid;
							break;
						}
						continue;
					}
					else {
						left = mid + 1;
						continue;
					}
				}
				if (dat[right % MD_BLOCK_NUM].NTimeMill > end_tp && right >= 1)
					--right;
				ret.end_index = right;
				ret.end_nTime = dat[right % MD_BLOCK_NUM].NTimeMill;
				ret.start_index = ret.end_index;
				ret.start_nTime = ret.end_nTime;
			}
			ret.dat = dat;
		}
		return ret;
	}

	md_ret get_md(const char* instrument, uint32_t len, uint64_t start_tp, uint64_t end_tp, uint64_t offset_sec) {
		md_ret ret;
		ret.ret = false;
		if (start_tp > end_tp)
			return ret;

		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument, len));
		if (iter != m_instruments_address.end()) {
			market_data* dat = iter->second->dat;
			uint64_t end = iter->second->current_index - 1;
			uint64_t start = (end + 1 >= MD_BLOCK_NUM) ? (end + 1 - MD_BLOCK_NUM) : 0;

			int64_t start_short = (static_cast<int64_t>(end) - 4 * (static_cast<int64_t>(offset_sec) + 2) < 0) ? 0 : static_cast<int64_t>(end) - 4 * (static_cast<int64_t>(offset_sec) + 2);
			// std::cout << "end:" << end << " offsec:" << offset_sec << std::endl;

			market_data* end_dat = &dat[end % MD_BLOCK_NUM];
			market_data* start_dat_long = &dat[start % MD_BLOCK_NUM];
			market_data* start_dat_short = &dat[start_short % MD_BLOCK_NUM];
			if (end_dat->NTimeMill< start_tp || start_dat_long->NTimeMill> end_tp) {
				return ret;
			}

			ret.ret = true;
			ret.buff_len = MD_BLOCK_NUM;
			
			if (end_tp >= end_dat->NTimeMill) {
				ret.end_index = end;
				ret.end_nTime = end_dat->NTimeMill;
			}
			else {
				int64_t left = (start_dat_short->NTimeMill >= end_tp) ? start : start_short, right = end;
				// left = (start_dat_short->NTimeMill >= end_tp) ? start : start_short;
				// right = end;
				while (left < right) {
					auto mid = (left + right) / 2;
					auto tp = dat[mid % MD_BLOCK_NUM].NTimeMill;
					if (tp == end_tp) {
						right = mid;
						break;
					}
					if (tp > end_tp) {
						right = mid - 1;
						if (right < 0) {
							right = mid;
							break;
						}
						continue;
					}
					else {
						left = mid + 1;
						continue;
					}
				}
				if (dat[right % MD_BLOCK_NUM].NTimeMill > end_tp && right >= 1)
					--right;
				ret.end_index = right;
				ret.end_nTime = dat[right % MD_BLOCK_NUM].NTimeMill;
			}

			int64_t left = (start_dat_short->NTimeMill >= start_tp) ? start : start_short, right = end;
			
			if (start_tp <= start_dat_long->NTimeMill) {
				ret.start_index = start;
				ret.start_nTime = start_dat_long->NTimeMill;
			}
			else {
				while (left < right) {
					auto mid = (left + right) / 2;
					auto tp = dat[mid % MD_BLOCK_NUM].NTimeMill;
					if (tp == start_tp) {
						right = mid;
						break;
					}
					if (tp > start_tp) {
						right = mid - 1;
						if (right < 0) {
							right = mid;
							break;
						}
						continue;
					}
					else {
						left = mid + 1;
						continue;
					}
				}
				if (dat[right % MD_BLOCK_NUM].NTimeMill < start_tp && right < end)
					++right;
				ret.start_index = right;
				ret.start_nTime = dat[right % MD_BLOCK_NUM].NTimeMill;
			}

			ret.dat = dat;
		}

		return ret;
	}

	md_ret get_md(const char* instrument, uint32_t len, uint64_t start_tp, uint64_t end_tp) {
		uint64_t offset_sec = cal_offset_sec(start_tp, end_tp);
		return get_md(instrument, len, start_tp, end_tp, offset_sec);
	}

	md_ret get_md(const char* instrument, int seconds_offset) {
		md_ret ret;
		ret.ret = false;
		market_data* md = get_cur_md(instrument,strlen(instrument));
		if (md == nullptr) {
			return ret;
		}
		uint64_t end_tp = md->NTimeMill;
		uint64_t start_tp = convert_to_epoch(std::to_string(end_tp), seconds_offset);
		//std::cout<<start_tp<<" "<<end_tp<<std::endl;
		return get_md(instrument, strlen(instrument), start_tp, end_tp);
	}

	md_ret get_md_all(const char* instrument, uint32_t len) {
		md_ret ret;
		ret.ret = false;
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument, len));
		if (iter != m_instruments_address.end()) {
			market_data* dat = iter->second->dat;
			uint64_t end = iter->second->current_index - 1;
			uint64_t start = (end + 1 >= MD_BLOCK_NUM) ? (end + 1 - MD_BLOCK_NUM) : 0;
			ret.buff_len = MD_BLOCK_NUM;
			market_data* start_dat = &dat[start % MD_BLOCK_NUM];
			market_data* end_dat = &dat[end % MD_BLOCK_NUM];
			ret.start_index = start;
			ret.end_index = end;
			ret.end_nTime = end_dat->NTimeMill;
			ret.start_nTime = start_dat->NTimeMill;
			ret.dat = dat;
			ret.ret = true;
		}
		return ret;
	}

	uint64_t get_update_num(const std::string& instrument) {
		auto iter = m_instruments_address.find(md_shm_util::com_hash(instrument.data(), instrument.length()));
		if (iter != m_instruments_address.end()) {
			return iter->second->current_index;
		}
		return 0;
	}

	bool on_rtn_data(const char* instrumentID, const market_data& newData, std::function<bool(const market_data* old_md, const market_data* new_md)> compare = nullptr) {
		if (m_shm_head == nullptr)
			return false;

		long long security_no = md_shm_util::com_hash(instrumentID, strlen(instrumentID));
		auto iter = m_instruments_address.find(security_no);
		if (iter == m_instruments_address.end()) {
			if (m_md_num < (m_max_instrument_num - 1)) {
				std::string id(instrumentID);
				init_instrument_shm(m_next_empty_md, id);
				m_next_empty_md = (md_head_plus*)((market_data*)(m_next_empty_md + 1) + MD_BLOCK_NUM);
				iter = m_instruments_address.find(security_no);
				++m_md_num;
			}
			else {
				return false;
			}
		}
		md_head_plus* index = iter->second;
		bool updated = true;
		if (compare != nullptr) {
			market_data* old_current = (market_data*)(index + 1) + index->current_index % MD_BLOCK_NUM;
			updated = compare(old_current, &newData);
		}
		if (updated) {
			market_data* data = &(index->dat[(index->current_index) % MD_BLOCK_NUM]);
			*data = newData;
			mb();
			++index->current_index;
			++m_shm_head->update_num;
			return true;
		}
		return false;
	}

	md_head_plus* sub_md(const char* instrumentID) {
		if (m_shm_head == nullptr)
			return nullptr;

		long long security_no = md_shm_util::com_hash(instrumentID, strlen(instrumentID));
		auto iter = m_instruments_address.find(security_no);
		if (iter == m_instruments_address.end()) {
			return nullptr;
		}
		md_head_plus* index = iter->second;
		return index;
	}

	void set_md_dump_func(std::function<void(void*, market_data*)> fun) {
		m_md_dump_fun = fun;
	}

private:
	md_shm_head_plus* m_shm_head;
	md_head_plus* m_md_head, * m_next_empty_md;
	int m_max_instrument_num = 0;
	int m_md_num = 0;
	uint64_t m_shm_size = 0;
	int MD_BLOCK_NUM;

#ifdef __linux__
	ska::flat_hash_map<unsigned long long, md_head_plus*> m_instruments_address;
#else
	std::unordered_map<unsigned long long, md_head_plus*> m_instruments_address;
#endif
	std::function<void(void*, market_data*)> m_md_dump_fun;
};