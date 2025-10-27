#pragma once

#include "K4_WsSslClient.h"
#include "K4_Struct.h"
#include "K4_ShmMdPlus.h"
#include "K4_LoggerBase.h"
#include "K4_Util.h"
#include "K4_Common.h"
#include "K4_MDBase.h"

#include <functional>
#include <iostream>
#include <memory>
#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#define BINANCE_WS_HOST "stream.binance.com"
#define BINANCE_WS_UB_HOST "fstream.binance.com"
#define BINANCE_WS_CB_HOST "dstream.binance.com"
#define WS_PORT 443

#define TEST_BINANCE_WS_HOST "stream.testnet.binance.vision"
#define TEST_BINANCE_WS_UB_HOST "fstream.binancefuture.com"
#define TEST_BINANCE_WS_CB_HOST "dstream.binancefuture.com"

using namespace mht_k4;

class binance_md
{
public:
	binance_md(YAML::Node &yml_config) {
		
	}
	virtual ~binance_md() {}

	void init_bookTicker_shm(std::string spot_shm_name, std::string fut_shm_name, 
        std::vector<std::string>& spot_sub, std::vector<std::string>& fut_sub, int spot_num,int fut_num,int buff_len) {
		m_spot_shm = new shm_md_plus<K4BookTicker>();
		void* ptr = m_spot_shm->create_shm_ptr(spot_shm_name, spot_num,buff_len,true);
		m_spot_shm->set_shm_addr((md_shm_head_plus*)ptr, spot_sub, true);

		m_fut_shm = new shm_md_plus<K4BookTicker>();
		void* ptr2 = m_fut_shm->create_shm_ptr(fut_shm_name, fut_num,buff_len,true);
		m_fut_shm->set_shm_addr((md_shm_head_plus*)ptr2, fut_sub, true);
	}

	void init_depth10_shm(std::string spot_shm_name, std::string fut_shm_name, 
        std::vector<std::string>& spot_sub, std::vector<std::string>& fut_sub, int spot_num,int fut_num,int buff_len) {
		m_spot_depthshm = new shm_md_plus<K4Depth10Ticker>();
		void* ptr = m_spot_depthshm->create_shm_ptr(spot_shm_name, spot_num, buff_len, true);
		m_spot_depthshm->set_shm_addr((md_shm_head_plus*)ptr, spot_sub, true);

		m_fut_depthshm = new shm_md_plus<K4Depth10Ticker>();
		void* ptr2 = m_fut_depthshm->create_shm_ptr(fut_shm_name, fut_num, buff_len, true);
		m_fut_depthshm->set_shm_addr((md_shm_head_plus*)ptr2, fut_sub, true);
	}

	void on_recon(std::string& msg) {SPDLOG_WARN("bnce ws reconn:{}",msg);}

	void on_conn(int id) {}

	void on_closed(std::string& msg) {SPDLOG_WARN("bnce ws closed:{}",msg);}

    void parse_spot_bookTicker(rapidjson::Document &doc){
        K4BookTicker ticker;
		ticker.type = K4InstrumentType::SPOT;
		ticker.exch = K4Exchange::BINANCE;

        auto symbol = get_k4_symbol(doc["s"].GetString());
        std::strncpy(ticker.symbol, symbol.data(), sizeof(ticker.symbol) - 1);
        ticker.SrvTime_us = doc["u"].GetUint64();
		ticker.LocalTime_us = get_current_ms_epoch()*1000;
        ticker.ask_price = atof(doc["a"].GetString());
        ticker.ask_vol = atof(doc["A"].GetString());
        ticker.bid_price = atof(doc["b"].GetString());
        ticker.bid_vol = atof(doc["B"].GetString());

		//printf("on spot bookticker md:%s\n",ticker.symbol);
        if(m_spot_shm != nullptr){
            m_spot_shm->on_rtn_data(ticker.symbol,ticker);
        }
    }

    void parse_ubase_bookTicker(rapidjson::Document &doc){
        K4BookTicker ticker;
		ticker.type = K4InstrumentType::CRYPTO_USDTBASE;
		ticker.exch = K4Exchange::BINANCE;
        
		auto symbol = get_k4_symbol(doc["s"].GetString());
        std::strncpy(ticker.symbol, symbol.data(), sizeof(ticker.symbol) - 1);
        ticker.SrvTime_us = doc["u"].GetUint64();
		ticker.LocalTime_us = get_current_ms_epoch()*1000;
        ticker.ask_price = atof(doc["a"].GetString());
        ticker.ask_vol = atof(doc["A"].GetString());
        ticker.bid_price = atof(doc["b"].GetString());
        ticker.bid_vol = atof(doc["B"].GetString());

        if(m_fut_shm != nullptr){
			//std::cout<<"wr md:"<<ticker.symbol<<",ask px:"<<ticker.ask_price<<std::endl;
            bool ret = m_fut_shm->on_rtn_data(ticker.symbol,ticker);
			if(!ret)
				std::cout<<"wr ubase fail:"<<ticker.symbol<<std::endl;
        }
    }

    void parse_cbase_bookTicker(rapidjson::Document &doc){
        K4BookTicker ticker;
		ticker.type = K4InstrumentType::CRYPTO_COINBASE;
		ticker.exch = K4Exchange::BINANCE;
        
		auto symbol = get_k4_symbol(doc["s"].GetString());
        std::strncpy(ticker.symbol, symbol.data(), sizeof(ticker.symbol) - 1);
        ticker.SrvTime_us = doc["u"].GetUint64();
		ticker.LocalTime_us = get_current_ms_epoch()*1000;
        ticker.ask_price = atof(doc["a"].GetString());
        ticker.ask_vol = atof(doc["A"].GetString());
        ticker.bid_price = atof(doc["b"].GetString());
        ticker.bid_vol = atof(doc["B"].GetString());

        if(m_fut_shm != nullptr){
            m_fut_shm->on_rtn_data(ticker.symbol,ticker);
        }
    }

	void parse_ubase_depth10(rapidjson::Document &doc){
		K4Depth10Ticker ticker = parse_depth10(doc);
		ticker.type = K4InstrumentType::CRYPTO_USDTBASE;
        if(m_fut_depthshm != nullptr){
            bool ret = m_fut_depthshm->on_rtn_data(ticker.symbol,ticker);
			if(!ret)
				std::cout<<"wr ubase fail:"<<ticker.symbol<<std::endl;
        }
    }

    void parse_cbase_depth10(rapidjson::Document &doc){
        K4Depth10Ticker ticker = parse_depth10(doc);
		ticker.type = K4InstrumentType::CRYPTO_COINBASE;
        if(m_fut_depthshm != nullptr){
            m_fut_depthshm->on_rtn_data(ticker.symbol,ticker);
        }
    }

	std::string extract_symbol_upper(const std::string& stream_str) {
		auto at_pos = stream_str.find('@');
		std::string symbol = (at_pos != std::string::npos) ? stream_str.substr(0, at_pos) : stream_str;

		std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
		return symbol;
	}

	void parse_spot_depth10(rapidjson::Document &doc){
		std::string symbol = extract_symbol_upper(doc["stream"].GetString());
		K4Depth10Ticker ticker;

		auto symbol2 = get_k4_symbol(symbol);
		std::strncpy(ticker.symbol, symbol2.data(), sizeof(ticker.symbol) - 1);
		ticker.SrvTime_us = get_current_ms_epoch()*1000;
		ticker.LocalTime_us = get_current_ms_epoch()*1000;
		ticker.exch = K4Exchange::BINANCE;

		if (doc.HasMember("data") && doc["data"]["bids"].IsArray()) {
			int i = 0;
			const auto& bids = doc["data"]["bids"].GetArray();
			for (const auto& bid : bids) {
				if (bid.IsArray() && bid.Size() >= 2) {
					ticker.bid_price[i] = atof(bid[0].GetString());
					ticker.bid_vol[i] = atof(bid[1].GetString());
					++i;
					if(i>=10)
						break;
				}
        	}
    	}

		if (doc.HasMember("data") && doc["data"]["asks"].IsArray()) {
			int i = 0;
			const auto& asks = doc["data"]["asks"].GetArray();
			for (const auto& ask : asks) {
				if (ask.IsArray() && ask.Size() >= 2) {
					ticker.ask_price[i] = atof(ask[0].GetString());
					ticker.ask_vol[i] = atof(ask[1].GetString());
					++i;
					if(i>=10)
						break;
				}
			}
		}

		if(m_spot_depthshm!=nullptr)
			m_spot_depthshm->on_rtn_data(ticker.symbol,ticker);
	}

	K4Depth10Ticker parse_depth10(rapidjson::Document &doc){
		K4Depth10Ticker ticker;
		auto symbol = get_k4_symbol(doc["s"].GetString());
		std::strncpy(ticker.symbol, symbol.data(), sizeof(ticker.symbol) - 1);
		ticker.SrvTime_us = doc["T"].GetUint64();
		ticker.LocalTime_us = get_current_ms_epoch()*1000;
		ticker.exch = K4Exchange::BINANCE;

		if (doc.HasMember("b") && doc["b"].IsArray()) {
			int i = 0;
			const auto& bids = doc["b"].GetArray();
			for (const auto& bid : bids) {
				if (bid.IsArray() && bid.Size() >= 2) {
					ticker.bid_price[i] = atof(bid[0].GetString());
					ticker.bid_vol[i] = atof(bid[1].GetString());
					//std::cout << "Bid Price: " << bid[0].GetString()<< ", Quantity: " << bid[1].GetString() << "\n";
					++i;
					if(i>=10)
						break;
				}
        	}
    	}

		if (doc.HasMember("a") && doc["a"].IsArray()) {
			int i = 0;
			const auto& asks = doc["a"].GetArray();
			for (const auto& ask : asks) {
				if (ask.IsArray() && ask.Size() >= 2) {
					ticker.ask_price[i] = atof(ask[0].GetString());
					ticker.ask_vol[i] = atof(ask[1].GetString());
					//std::cout << "Ask Price: " << ask[0].GetString()<< ", Quantity: " << ask[1].GetString() << "\n";
					++i;
					if(i>=10)
						break;
				}
			}
		}
		
		return ticker;
	}

	bool on_spot_md(char* data, size_t len, int id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
		//if(len<50)
			//std::cout<<data<<std::endl;
        if(doc.HasMember("u")){
            parse_spot_bookTicker(doc);
        }
		else if(doc.HasMember("stream")){
			parse_spot_depth10(doc);
		}
		return true;
	}

	bool on_ubase_md(char* data, size_t len, int id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
		//if(len<50)
			//std::cout<<data<<std::endl;
        if(doc.HasMember("e")){
            std::string ev = doc["e"].GetString();
            if(ev=="bookTicker"){
                parse_ubase_bookTicker(doc);
            }
			else if(ev=="depthUpdate"){
				parse_ubase_depth10(doc);
			}
        }
		return true;
	}

	bool on_cbase_md(char* data, size_t len, int id) {
		if (len < 30)
			return true;
        rapidjson::Document doc;
        doc.Parse(data, len);
		//if(len<50)
			//std::cout<<data<<std::endl;
		if(doc.HasMember("e")){
            std::string ev = doc["e"].GetString();
            if(ev=="bookTicker"){
                parse_ubase_bookTicker(doc);
            }
			else if(ev=="depthUpdate"){
				parse_cbase_depth10(doc);
			}
        }

		return true;
	}

	bool create_binance_spot_md() {
		ws_client_callback cb;
		cb.on_closed = std::bind(&binance_md::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&binance_md::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&binance_md::on_spot_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&binance_md::on_recon, this, std::placeholders::_1);

		m_spot = std::make_shared<ws_ssl_client>(1, TEST_BINANCE_WS_HOST, "443", cb);
		m_spot_combined_stream = std::make_shared<ws_ssl_client>(11, TEST_BINANCE_WS_HOST, "443", cb);
		//连接币安ws服务
		m_spot->connect("/ws");
		//启动boost的网络线程
		m_spot->start_work();

		//等等连接成功
		int try_num = 0;
		while (1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++try_num >= m_max_try) {
				std::cout << "can not conn bnce ubase ws\n";
				exit(0);
			}
			if (m_spot->is_ready()) {
				break;
			}
		}
		SPDLOG_INFO("create_binance_spot_md fin");
		return true;
	}

	bool create_binance_ubase_md() {
		ws_client_callback cb;
		cb.on_closed = std::bind(&binance_md::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&binance_md::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&binance_md::on_ubase_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&binance_md::on_recon, this, std::placeholders::_1);

		m_ubase = std::make_shared<ws_ssl_client>(1, TEST_BINANCE_WS_UB_HOST, "443", cb);
		//连接币安ws服务
		m_ubase->connect("/ws");
		//启动boost的网络线程
		m_ubase->start_work();

		//等等连接成功
		int try_num = 0;
		while (1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++try_num >= m_max_try) {
				std::cout << "can not conn bnce ubase ws\n";
				exit(0);
			}
			if (m_ubase->is_ready()) {
				break;
			}
		}
		SPDLOG_INFO("create_binance_ubase_md fin");
		return true;
	}

	bool create_binance_cbase_md() {
		ws_client_callback cb;
		cb.on_closed = std::bind(&binance_md::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&binance_md::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&binance_md::on_cbase_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&binance_md::on_recon, this, std::placeholders::_1);

		m_cbase = std::make_shared<ws_ssl_client>(1, BINANCE_WS_CB_HOST, "443", cb);
		//连接币安ws服务
		m_cbase->connect("/ws");
		//启动boost的网络线程
		m_cbase->start_work();

		//等等连接成功
		int try_num = 0;
		while (1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++try_num >= m_max_try) {
				std::cout << "can not conn bnce ubase ws\n";
				exit(0);
			}
			if (m_cbase->is_ready()) {
				break;
			}
		}

		SPDLOG_INFO("create_binance_cbase_md fin");
		return true;
	}

	void sub_ubase_book_ticker(std::vector<std::string>& vec) {
		if (vec.size() == 0)
			return;

		std::string sub = "{\
		\"method\": \"SUBSCRIBE\",\
		\"id\":1,\
		\"params\" :";
		std::string append = "[";

		bool is_1st = true;
		for (auto& it : vec) {
			if (is_1st) {
				append += ("\"" + it + "@bookTicker\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@bookTicker\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		SPDLOG_INFO("sub_ubase_book_ticker {}",msg);
		m_ubase->send(msg);
	}

	void sub_spot_book_ticker(std::vector<std::string>& vec) {
		if (vec.size() == 0)
			return;

		std::string sub = "{\
		\"method\": \"SUBSCRIBE\",\
		\"id\":1,\
		\"params\" :";
		std::string append = "[";

		bool is_1st = true;
		for (auto& it : vec) {
			if (is_1st) {
				append += ("\"" + it + "@bookTicker\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@bookTicker\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		SPDLOG_INFO("sub_spot_book_ticker {}",msg);
		m_spot->send(msg);
	}

	void sub_cbase_book_ticker(std::vector<std::string>& vec) {
		if (vec.size() == 0)
			return;

		std::string sub = "{\
		\"method\": \"SUBSCRIBE\",\
		\"id\":1,\
		\"params\" :";
		std::string append = "[";

		bool is_1st = true;
		for (auto& it : vec) {
			if (is_1st) {
				append += ("\"" + it + "@bookTicker\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@bookTicker\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		SPDLOG_INFO("sub_cbase_book_ticker {}",msg);
		m_cbase->send(msg);
	}

	void sub_spot_depth10(std::vector<std::string>& symbols) {
		if (symbols.empty()) return;

		std::string combined_uri = "/stream?streams=";
		for (size_t i = 0; i < symbols.size(); ++i) {
			if (i > 0) combined_uri += "/";
			combined_uri += symbols[i] + "@depth10@100ms";
		}

		m_spot_combined_stream->reset_uri_(combined_uri);  // 设置 uri
		m_spot_combined_stream->connect(combined_uri);     // 发起连接（最终拼接成完整的 wss://.../stream?streams=... 由 ws_ssl_client 决定）
		m_spot_combined_stream->start_work(); 
	}

	void sub_ubase_depth10(std::vector<std::string>& vec) {
		if (vec.size() == 0)
			return;

		std::string sub = "{\
		\"method\": \"SUBSCRIBE\",\
		\"id\":2,\
		\"params\" :";
		std::string append = "[";

		bool is_1st = true;
		for (auto& it : vec) {
			if (is_1st) {
				append += ("\"" + it + "@depth10@100ms\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@depth10@100ms\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		SPDLOG_INFO("sub_ubase_depth10 {}",msg);
		m_ubase->send(msg);
	}

	void sub_cbase_depth10(std::vector<std::string>& vec) {
		if (vec.size() == 0)
			return;

		std::string sub = "{\
		\"method\": \"SUBSCRIBE\",\
		\"id\":2,\
		\"params\" :";
		std::string append = "[";

		bool is_1st = true;
		for (auto& it : vec) {
			if (is_1st) {
				append += ("\"" + it + "@depth10@100ms\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@depth10@100ms\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		SPDLOG_INFO("sub_cbase_depth10 {}",msg);
		m_cbase->send(msg);
	}

	void set_maping(const std::string &bnsymbol,const std::string &k4symbol){
		m_symbols[bnsymbol] = k4symbol;
	}

	std::string get_k4_symbol(const std::string &bnsymbol){
		auto it = m_symbols.find(bnsymbol);
		if(it!=m_symbols.end())
			return it->second;
		return "";
	}

private:
	std::shared_ptr<ws_ssl_client> m_spot = nullptr;
	std::shared_ptr<ws_ssl_client> m_spot_combined_stream = nullptr;
	std::shared_ptr<ws_ssl_client> m_ubase = nullptr;
	std::shared_ptr<ws_ssl_client> m_cbase = nullptr;

	shm_md_plus<K4BookTicker>* m_spot_shm = nullptr;
	shm_md_plus<K4BookTicker>* m_fut_shm = nullptr;
	shm_md_plus<K4Depth10Ticker>* m_spot_depthshm = nullptr;
	shm_md_plus<K4Depth10Ticker>* m_fut_depthshm = nullptr;

	ska::flat_hash_map<std::string,std::string> m_symbols;
    int m_max_try = 30;
};
