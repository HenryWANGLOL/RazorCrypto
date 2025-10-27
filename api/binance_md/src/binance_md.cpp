#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>
#include <cmath> 
#include <iomanip>
#include <sstream>
#include <cstring>
#include <memory>
#include <thread> // 添加线程库
#include <memory> // 确保包含智能指针支持
#include <boost/algorithm/string.hpp>
#include <chrono>
#include "RT_Misc.h"
#include "RT_CryptoMDBase.h"
#include "RT_CryptoDatatype.h"
#include "RT_Common.h"
#include "RT_MQ.h"
#include "csv.h"

#define BINANCE_WS_HOST "stream.binance.com"
#define BINANCE_WS_UB_HOST "fstream.binance.com"
#define BINANCE_WS_CB_HOST "dstream.binance.com"
#define WS_PORT 443

#define TEST_BINANCE_WS_HOST "stream.testnet.binance.vision"
#define TEST_BINANCE_WS_UB_HOST "fstream.binancefuture.com"
#define TEST_BINANCE_WS_CB_HOST "dstream.binancefuture.com"

using namespace mht_rt;

class Binance_MD : public RT_CryptoMDBase
{
public:
    Binance_MD(rapidjson::Document &json_config) : RT_CryptoMDBase(json_config)
    {
        if (json_config.HasMember("spot") && json_config["spot"].IsObject()) {
            const auto& spot = json_config["spot"];
            if (spot.HasMember(""))
            if (spot.HasMember("symbols") && spot["symbols"].IsArray()) {
                const auto& arr = spot["symbols"];
                for (auto& v : arr.GetArray()) {
                    if (v.IsString()) {
                        spot_symbols.emplace_back(v.GetString());
                    }
                }
            }

            if (spot_symbols.size() > 0) {
                spot_bbo_shm_name = spot.HasMember("bbo_shm_name") && spot["bbo_shm_name"].IsString() ? spot["bbo_shm_name"].GetString() : "spot_bbo";
                spot_aggtrade_shm_name = spot.HasMember("aggtrade_shm_name") && spot["aggtrade_shm_name"].IsString() ? spot["aggtrade_shm_name"].GetString() : "spot_aggtrade";
            }
        }

        if (json_config.HasMember("futures") && json_config["futures"].IsObject()) {
            const auto& fut = json_config["futures"];
            if (fut.HasMember("ubase_symbols") && fut["ubase_symbols"].IsArray()) {
                const auto& arr = fut["ubase_symbols"];
                for (auto& v : arr.GetArray()) {
                    if (v.IsString()) {
                        future_symbols.emplace_back(v.GetString());
                        ubase_symbols.emplace_back(v.GetString());
                    }
                }
            }

            if (fut.HasMember("cbase_symbols") && fut["cbase_symbols"].IsArray()) {
                const auto& arr = fut["cbase_symbols"];
                for (auto& v : arr.GetArray()) {
                    if (v.IsString()) {
                        future_symbols.emplace_back(v.GetString());
                        cbase_symbols.emplace_back(v.GetString());
                    }
                }
            }

            if (future_symbols.size() > 0) {
                fut_bbo_shm_name = fut.HasMember("bbo_shm_name") && fut["bbo_shm_name"].IsString() ? fut["bbo_shm_name"].GetString() : "future_bbo";
                fut_aggtrade_shm_name = fut.HasMember("aggtrade_shm_name") && fut["aggtrade_shm_name"].IsString() ? fut["aggtrade_shm_name"].GetString() : "future_aggtrade";
            }
        }

        md_size = json_config.HasMember("md_size") && json_config["md_size"].IsInt() ? json_config["md_size"].GetInt() : 100000;

        init_shm();
    }

    void init_shm() {
        init_bookTicker_shm(spot_bbo_shm_name, fut_bbo_shm_name, spot_symbols, future_symbols, spot_symbols.size(), future_symbols.size(), md_size);
        init_aggTrade_shm(spot_bbo_shm_name, fut_bbo_shm_name, spot_symbols, future_symbols, spot_symbols.size(), future_symbols.size(), md_size);
    }

    void init_bookTicker_shm(std::string spot_shm_name, std::string fut_shm_name, 
        std::vector<std::string>& spot_sub, std::vector<std::string>& fut_sub, int spot_num,int fut_num, int buff_len) {
        if (spot_num > 0) {
            m_spot_aggtrade_shm = new shm_md_plus<AggTrade>();
            std::vector<std::string> upper_spot = vector_upper(spot_sub);
            void* ptr = m_spot_aggtrade_shm->create_shm_ptr(spot_shm_name, spot_num, buff_len, true);
            m_spot_aggtrade_shm->set_shm_addr((md_shm_head_plus*)ptr, upper_spot, true);
        }

        if (fut_num > 0) {
            m_fut_aggtrade_shm = new shm_md_plus<AggTrade>();
            std::vector<std::string> upper_fut = vector_upper(fut_sub);
            void* ptr2 = m_fut_aggtrade_shm->create_shm_ptr(fut_shm_name, fut_num, buff_len, true);
            m_fut_aggtrade_shm->set_shm_addr((md_shm_head_plus*)ptr2, upper_fut, true);
        }
	}

	void init_aggTrade_shm(std::string spot_shm_name, std::string fut_shm_name, 
        std::vector<std::string>& spot_sub, std::vector<std::string>& fut_sub, int spot_num, int fut_num, int buff_len) {
        if (spot_num > 0) {
            m_spot_aggtrade_shm = new shm_md_plus<AggTrade>();
            std::vector<std::string> upper_spot = vector_upper(spot_sub);
            void* ptr = m_spot_aggtrade_shm->create_shm_ptr(spot_shm_name, spot_num, buff_len, true);
            m_spot_aggtrade_shm->set_shm_addr((md_shm_head_plus*)ptr, upper_spot, true);
        }

        if (fut_num > 0) {
            m_fut_aggtrade_shm = new shm_md_plus<AggTrade>();
            std::vector<std::string> upper_fut = vector_upper(fut_sub);
            void* ptr2 = m_fut_aggtrade_shm->create_shm_ptr(fut_shm_name, fut_num, buff_len, true);
            m_fut_aggtrade_shm->set_shm_addr((md_shm_head_plus*)ptr2, upper_fut, true);
        }

	}

    void on_recon(std::string& msg) {logWarn("binance ws reconn, {}", msg);}

    void on_conn(int id) {}

    void on_closed(std::string& msg) {logWarn("binance ws disconnect, {}", msg);}

    void parse_spot_bookTicker(rapidjson::Document &doc){
        Bookticker ticker;
		ticker.m_type = ProductType::SPOT;
		// ticker.exchange = ShortExchange::BINANCE;
        // ticker.exch

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(ticker.m_symbol, symbol.data(), sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = doc["u"].GetUint64();
		ticker.m_LocalTime_us = get_current_ms_epoch()*1000;
        ticker.m_eventTimestamp = doc["E"].GetUint64();
        ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = atof(doc["a"].GetString());
        ticker.m_askVol = atof(doc["A"].GetString());
        ticker.m_bidPrice = atof(doc["b"].GetString());
        ticker.m_bidVol = atof(doc["B"].GetString());

		//printf("on spot bookticker md:%s\n",ticker.symbol);
        if(m_spot_shm != nullptr){
            bool res = m_spot_shm->on_rtn_data(ticker.m_symbol,ticker);
            logInfo("insert data, res={}, symbol={}", res, ticker.m_symbol);
        }
    }

    void parse_ubase_bookTicker(rapidjson::Document &doc){
        Bookticker ticker;
		ticker.m_type = ProductType::UMARGIN;
		// ticker.exch = K4Exchange::BINANCE;
        
		auto symbol = std::string(doc["s"].GetString());
        std::strncpy(ticker.m_symbol, symbol.data(), sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = doc["u"].GetUint64();
		ticker.m_LocalTime_us = get_current_ms_epoch()*1000;
        ticker.m_eventTimestamp = doc["E"].GetUint64();
        ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = atof(doc["a"].GetString());
        ticker.m_askVol = atof(doc["A"].GetString());
        ticker.m_bidPrice = atof(doc["b"].GetString());
        ticker.m_bidVol = atof(doc["B"].GetString());

        if(m_fut_shm != nullptr){
			//std::cout<<"wr md:"<<ticker.symbol<<",ask px:"<<ticker.ask_price<<std::endl;
            bool ret = m_fut_shm->on_rtn_data(ticker.m_symbol,ticker);
            logInfo("insert data, res={}, symbol={}", ret, ticker.m_symbol);
			if(!ret)
				std::cout<<"wr ubase fail:"<<ticker.m_symbol<<std::endl;
        }
    }

    void parse_cbase_bookTicker(rapidjson::Document &doc){
        Bookticker ticker;
		ticker.m_type = ProductType::COINMARGIN;
		// ticker.exch = K4Exchange::BINANCE;
        
		auto symbol = std::string(doc["s"].GetString());
        std::strncpy(ticker.m_symbol, symbol.data(), sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = doc["u"].GetUint64();
		ticker.m_LocalTime_us = get_current_ms_epoch()*1000;
        ticker.m_eventTimestamp = doc["E"].GetUint64();
        ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = atof(doc["a"].GetString());
        ticker.m_askVol = atof(doc["A"].GetString());
        ticker.m_bidPrice = atof(doc["b"].GetString());
        ticker.m_bidVol = atof(doc["B"].GetString());

        if(m_fut_shm != nullptr){
            bool res = m_fut_shm->on_rtn_data(ticker.m_symbol,ticker);
            logInfo("insert data, res={}, symbol={}", res, ticker.m_symbol);
        }
    }

    void parse_spot_aggtrade(rapidjson::Document &doc) {
        AggTrade aggdata;
        aggdata.m_type = ProductType::SPOT;

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(aggdata.m_symbol, symbol.data(), sizeof(aggdata.m_symbol) - 1);
        aggdata.m_aggTradeId = doc["a"].GetUint64();
        aggdata.m_firstTradeId = doc["f"].GetUint64();
        aggdata.m_lastTradeId = doc["l"].GetUint64();
        aggdata.m_price = atof(doc["p"].GetString());
        aggdata.m_vol = atof(doc["q"].GetString());
        aggdata.m_isBuy = doc["m"].GetBool();
        aggdata.m_eventTimestamp = doc["E"].GetUint64();
        aggdata.m_tradeTimestamp = doc["T"].GetUint64();
        aggdata.m_LocalTime_us = get_current_ms_epoch() * 1000;

        if (m_spot_aggtrade_shm != nullptr) {
            bool res = m_spot_aggtrade_shm->on_rtn_data(aggdata.m_symbol, aggdata);
            logInfo("insert agg data, res={}, symbol={}", res, aggdata.m_symbol);
        }
    }

    void parse_ubase_aggtrade(rapidjson::Document &doc) {
        AggTrade aggdata;
        aggdata.m_type = ProductType::UMARGIN;

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(aggdata.m_symbol, symbol.data(), sizeof(aggdata.m_symbol) - 1);
        aggdata.m_aggTradeId = doc["a"].GetUint64();
        aggdata.m_firstTradeId = doc["f"].GetUint64();
        aggdata.m_lastTradeId = doc["l"].GetUint64();
        aggdata.m_price = atof(doc["p"].GetString());
        aggdata.m_vol = atof(doc["q"].GetString());
        aggdata.m_isBuy = doc["m"].GetBool();
        aggdata.m_eventTimestamp = doc["E"].GetUint64();
        aggdata.m_tradeTimestamp = doc["T"].GetUint64();
        aggdata.m_LocalTime_us = get_current_ms_epoch() * 1000;

        if (m_fut_aggtrade_shm != nullptr) {
            bool res = m_fut_aggtrade_shm->on_rtn_data(aggdata.m_symbol, aggdata);
            logInfo("insert agg data, res={}, symbol={}", res, aggdata.m_symbol);
        }
    }

    void parse_cbase_aggtrade(rapidjson::Document &doc) {
        AggTrade aggdata;
        aggdata.m_type = ProductType::COINMARGIN;

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(aggdata.m_symbol, symbol.data(), sizeof(aggdata.m_symbol) - 1);
        aggdata.m_aggTradeId = doc["a"].GetUint64();
        aggdata.m_firstTradeId = doc["f"].GetUint64();
        aggdata.m_lastTradeId = doc["l"].GetUint64();
        aggdata.m_price = atof(doc["p"].GetString());
        aggdata.m_vol = atof(doc["q"].GetString());
        aggdata.m_isBuy = doc["m"].GetBool();
        aggdata.m_eventTimestamp = doc["E"].GetUint64();
        aggdata.m_tradeTimestamp = doc["T"].GetUint64();
        aggdata.m_LocalTime_us = get_current_ms_epoch() * 1000;

        if (m_fut_aggtrade_shm != nullptr) {
            bool res = m_fut_aggtrade_shm->on_rtn_data(aggdata.m_symbol, aggdata);
            logInfo("insert agg data, res={}, symbol={}", res, aggdata.m_symbol);
        }
    }

    bool on_spot_md(char* data, size_t len, int id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
        if (doc.HasMember("e")) {
            std::string ev = std::string(doc["e"].GetString());
            if (ev == "aggTrade") {
                parse_spot_aggtrade(doc);
            }
            else if (ev == "bookTicker") {
                parse_spot_bookTicker(doc);
            }

        }
        return true;
    }

    bool on_ubase_md(char* data, size_t len, int id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
        if (doc.HasMember("e")) {
            std::string ev = std::string(doc["e"].GetString());
            if (ev == "aggTrade") {
                parse_ubase_aggtrade(doc);
            }
            else if (ev == "bookTicker") {
                parse_ubase_bookTicker(doc);
            }

        }
        return true;
    }

    bool on_cbase_md(char* data, size_t len, int id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
        if (doc.HasMember("e")) {
            std::string ev = std::string(doc["e"].GetString());
            if (ev == "aggTrade") {
                parse_cbase_aggtrade(doc);
            }
            else if (ev == "bookTicker") {
                parse_cbase_bookTicker(doc);
            }

        }
        return true;
    }

    bool create_binance_spot_md() {
        ws_client_callback cb;
        cb.on_closed = std::bind(&Binance_MD::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&Binance_MD::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&Binance_MD::on_spot_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&Binance_MD::on_recon, this, std::placeholders::_1);

        m_spot = std::make_shared<ws_ssl_client>(1, TEST_BINANCE_WS_HOST, "443", cb);
        //连接币安ws服务
		m_spot->connect("/ws");
		//启动boost的网络线程
		m_spot->start_work();

        //等等连接成功
		int try_num = 0;
		while (1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++try_num >= m_max_try) {
				std::cout << "can not conn bnce spot ws\n";
				exit(0);
			}
			if (m_spot->is_ready()) {
				break;
			}
		}
		logInfo("create_binance_spot_md fin");
		return true;
    }

    bool create_binance_ubase_md() {
        ws_client_callback cb;
        cb.on_closed = std::bind(&Binance_MD::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&Binance_MD::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&Binance_MD::on_ubase_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&Binance_MD::on_recon, this, std::placeholders::_1);

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
		logInfo("create_binance_ubase_md fin");
		return true;
    }

    bool create_binance_cbase_md() {
        ws_client_callback cb;
        cb.on_closed = std::bind(&Binance_MD::on_closed, this, std::placeholders::_1);
		cb.on_conn = std::bind(&Binance_MD::on_conn, this, std::placeholders::_1);
		cb.on_data_ready = std::bind(&Binance_MD::on_cbase_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3);
		cb.on_reconn = std::bind(&Binance_MD::on_recon, this, std::placeholders::_1);

        m_cbase = std::make_shared<ws_ssl_client>(1, TEST_BINANCE_WS_CB_HOST, "443", cb);
        //连接币安ws服务
		m_cbase->connect("/ws");
		//启动boost的网络线程
		m_cbase->start_work();

        //等等连接成功
		int try_num = 0;
		while (1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (++try_num >= m_max_try) {
				std::cout << "can not conn bnce cbase ws\n";
				exit(0);
			}
			if (m_cbase->is_ready()) {
				break;
			}
		}
		logInfo("create_binance_cbase_md fin");
		return true;
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
		logInfo("sub_spot_book_ticker {}",msg);
		m_spot->send(msg);
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
		logInfo("sub_ubase_book_ticker {}",msg);
		m_ubase->send(msg);
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
		logInfo("sub_cbase_book_ticker {}",msg);
		m_cbase->send(msg);
	}

    void sub_spot_agg_trade(std::vector<std::string>& vec) {
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
				append += ("\"" + it + "@aggTrade\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@aggTrade\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		logInfo("sub_spot_agg_trade {}",msg);
		m_spot->send(msg);
	}

    void sub_ubase_agg_trade(std::vector<std::string>& vec) {
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
				append += ("\"" + it + "@aggTrade\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@aggTrade\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		logInfo("sub_ubase_agg_trade {}",msg);
		m_ubase->send(msg);
	}

    void sub_cbase_agg_trade(std::vector<std::string>& vec) {
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
				append += ("\"" + it + "@aggTrade\"");
				is_1st = false;
			}
			else {
				append += (",\"" + it + "@aggTrade\"");
			}
		}

		append += "]}";
		std::string msg = sub + append;
		logInfo("sub_cbase_agg_trade {}",msg);
		m_cbase->send(msg);
	}

    int request_connect() {
        if (spot_symbols.size() > 0) {
            create_binance_spot_md();
            sub_spot_agg_trade(spot_symbols);
            sub_spot_book_ticker(spot_symbols);
        }

        if (ubase_symbols.size() > 0) {
            create_binance_ubase_md();
            sub_ubase_agg_trade(ubase_symbols);
            sub_ubase_book_ticker(ubase_symbols);
        }
        
        if (cbase_symbols.size() > 0) {
            create_binance_cbase_md();
            sub_cbase_agg_trade(cbase_symbols);
            sub_cbase_book_ticker(cbase_symbols);
        }

        m_bDoneConnect = true;
        return 1;
    }

    std::string to_uppercase(const std::string &str) {
        std::string result = str;  // 拷贝一份，不改原来的
        std::transform(result.begin(), result.end(), result.begin(),
                    [](unsigned char c) { return std::toupper(c); });
        return result;
    }

    std::vector<std::string> vector_upper(std::vector<std::string> input) {
        std::vector<std::string> res;
        for (int i = 0; i < input.size(); ++i) {
            res.push_back(to_uppercase(input[i]));
        }
        return res;
    }



private:
    ska::flat_hash_map<std::string, std::string> m_symbols;

    std::shared_ptr<ws_ssl_client> m_spot = nullptr;
	std::shared_ptr<ws_ssl_client> m_ubase = nullptr;
	std::shared_ptr<ws_ssl_client> m_cbase = nullptr;

	shm_md_plus<Bookticker>* m_spot_shm = nullptr;
	shm_md_plus<Bookticker>* m_fut_shm = nullptr;
	shm_md_plus<AggTrade>* m_spot_aggtrade_shm = nullptr;
	shm_md_plus<AggTrade>* m_fut_aggtrade_shm = nullptr;

    int m_max_try = 30;
    std::vector<std::string> spot_symbols;
    std::vector<std::string> future_symbols;
    std::vector<std::string> ubase_symbols;
    std::vector<std::string> cbase_symbols;

    std::string spot_bbo_shm_name;
    std::string spot_aggtrade_shm_name;
    std::string fut_bbo_shm_name;
    std::string fut_aggtrade_shm_name;

    int md_size;
};


extern "C" std::unique_ptr<RT_CryptoMDBase> create_mdlib(rapidjson::Document& json_config) {
    return std::unique_ptr<RT_CryptoMDBase>(new Binance_MD(json_config));
}