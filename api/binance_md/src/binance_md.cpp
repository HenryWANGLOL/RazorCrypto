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
#include <thread>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <sstream>

#include "boost/algorithm/string.hpp"
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/prettywriter.h" // 用于格式化输出

// 项目依赖头文件
#include "RT_Misc.h"         // 提供get_current_ms_epoch等工具函数
#include "RT_CryptoMDBase.h" // 基础行情类（已支持stop/on_stop）
#include "RT_CryptoDatatype.h"// 定义Bookticker、AggTrade等数据结构
#include "RT_Common.h"       // 提供logInfo、logWarn等日志函数
#include "K4_WsSslClient.h"       // 提供ws_ssl_client和ws_client_callback
#include "RT_ShmMdPlus.h"       // 提供ws_ssl_client和ws_client_callback

using namespace mht_rt;
using namespace rapidjson;

// ###########################################################################
// 1. 仅保留Spot和UBase（Future）的主机地址宏（删除CBase相关）
// ###########################################################################
// #define TEST_BINANCE_WS_HOST     "stream.testnet.binance.vision"    // Spot测试网
// #define TEST_BINANCE_WS_UB_HOST  "fstream.testnet.binance.vision"   // UBase合约（Future）测试网
// 正式网地址（如需切换，注释测试网，启用以下）
#define BINANCE_WS_HOST     "stream.binance.com"
#define BINANCE_WS_UB_HOST  "fstream.binance.com"

// ###########################################################################
// 2. 延迟统计结构体（无修改，仅统计Spot和UBase）
// ###########################################################################
// 修正后：支持移动，禁止拷贝
struct ConnLatencyStats {
    int conn_id;                          // 连接唯一ID
    std::string product_type;             // 产品类型（Spot/UBase）
    std::atomic<uint64_t> total_count{0}; // 总接收数据条数
    std::atomic<uint64_t> total_latency{0};// 总延迟（微秒）
    std::atomic<uint64_t> latest_latency{0};// 最新延迟（微秒）
    std::atomic<uint64_t> min_latency{UINT64_MAX};// 最小延迟（微秒）

    // 1. 禁止拷贝（必须显式删除）
    ConnLatencyStats(const ConnLatencyStats&) = delete;
    ConnLatencyStats& operator=(const ConnLatencyStats&) = delete;

    // 2. 显式定义移动构造（核心：手动转移atomic成员值）
    ConnLatencyStats(ConnLatencyStats&& src) noexcept
        : conn_id(src.conn_id),
          product_type(std::move(src.product_type)) {
        // atomic成员不可直接移动，用load()获取值，store()写入当前对象
        this->total_count.store(src.total_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
        this->total_latency.store(src.total_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);
        this->latest_latency.store(src.latest_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);
        this->min_latency.store(src.min_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);

        // 重置源对象（避免源对象析构时干扰）
        src.total_count.store(0, std::memory_order_relaxed);
        src.total_latency.store(0, std::memory_order_relaxed);
        src.latest_latency.store(0, std::memory_order_relaxed);
        src.min_latency.store(UINT64_MAX, std::memory_order_relaxed);
        src.conn_id = -1; // 标记源对象为无效
    }

    // 3. 显式定义移动赋值
    ConnLatencyStats& operator=(ConnLatencyStats&& src) noexcept {
        if (this != &src) {
            // 转移非atomic成员
            this->conn_id = src.conn_id;
            this->product_type = std::move(src.product_type);

            // 转移atomic成员
            this->total_count.store(src.total_count.load(std::memory_order_relaxed), std::memory_order_relaxed);
            this->total_latency.store(src.total_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);
            this->latest_latency.store(src.latest_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);
            this->min_latency.store(src.min_latency.load(std::memory_order_relaxed), std::memory_order_relaxed);

            // 重置源对象
            src.total_count.store(0, std::memory_order_relaxed);
            src.total_latency.store(0, std::memory_order_relaxed);
            src.latest_latency.store(0, std::memory_order_relaxed);
            src.min_latency.store(UINT64_MAX, std::memory_order_relaxed);
            src.conn_id = -1;
        }
        return *this;
    }

    // 4. 默认构造（必须保留）
    ConnLatencyStats() = default;

    // 5. 带参构造（用于直接初始化，避免赋值）
    ConnLatencyStats(int id, std::string type)
        : conn_id(id), product_type(std::move(type)) {}

    // 计算平均延迟
    double get_avg_latency() const {
        return total_count > 0 ? (double)total_latency / total_count : 0.0;
    }
};
// 全局延迟统计容器（仅存Spot/UBase连接）
static std::unordered_map<int, ConnLatencyStats> g_conn_latency_stats;

// ###########################################################################
// 3. Binance_MD类（核心：删除CBase逻辑，保留Spot+UBase）
// ###########################################################################
class Binance_MD : public RT_CryptoMDBase {
public:
    Binance_MD(Document& json_config) : RT_CryptoMDBase(json_config), 
                                        m_json_config(json_config),
                                        m_print_running(true) {

        printf("initing Binance md, parsing config... \n");

        StringBuffer prettyBuffer;
        PrettyWriter<StringBuffer> prettyWriter(prettyBuffer); // 格式化Writer
        prettyWriter.SetIndent(' ', 4); // 设置缩进（空格+4个空格）
        json_config.Accept(prettyWriter); // 写入缓冲区
        std::cout << "\n格式化格式:\n" << prettyBuffer.GetString() << std::endl;
        parse_config();   // 仅解析Spot和UBase配置
        init_shm();       // 仅初始化Spot和UBase共享内存
    }

    ~Binance_MD() override = default; // 基类析构自动触发on_stop释放资源

    // 重写基类：仅创建Spot和UBase连接
    int request_connect() override {
        create_spot_connections();   // Spot多连接
        create_ubase_connections();  // UBase（Future）多连接
        start_latency_print_timer(); // 延迟统计（仅Spot/UBase）

        m_bDoneConnect = true;
        logInfo("所有连接初始化完成：Spot({}), UBase(Future)({})",
            m_spot_conns.size(), m_ubase_conns.size());
        return 0;
    }

    // 重写基类on_stop：仅释放Spot和UBase资源
    void on_stop() override {
        logInfo("开始释放Binance行情资源（Spot+UBase）...");

        // 1. 停止延迟打印线程
        m_print_running = false;
        if (m_print_thread.joinable()) {
            m_print_thread.join();
            logInfo("延迟打印线程已停止");
        }

        // 2. 停止Spot和UBase的WebSocket连接
        stop_all_connections();

        // 3. 清空连接容器（智能指针自动释放）
        m_spot_conns.clear();
        m_ubase_conns.clear();

        // 4. 清空统计容器
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        g_conn_latency_stats.clear();

        logInfo("Binance行情资源（Spot+UBase）已全部释放");
    }

private:
    // #######################################################################
    // 配置解析：删除CBase相关，仅保留Spot和UBase
    // #######################################################################
    void parse_config() {
        //打印config

        // -------------------------- Spot配置 --------------------------
        if (m_json_config.HasMember("spot") && m_json_config["spot"].IsObject()) {
            const auto& spot = m_json_config["spot"];
            printf("here 1 \n");
            if (spot.HasMember("symbols") && spot["symbols"].IsArray()) {
                for (const auto& v : spot["symbols"].GetArray()) {
                    if (v.IsString()) spot_symbols.emplace_back(v.GetString());
                }
            }

            // 解析Spot连接数量（默认1个）
            m_spot_conn_count = spot.HasMember("conn_count") && spot["conn_count"].IsInt()
                ? spot["conn_count"].GetInt()
                : 1;
            // 解析Spot共享内存名称

            spot_bbo_shm_name = spot.HasMember("bbo_shm_name") && spot["bbo_shm_name"].IsString()
                ? spot["bbo_shm_name"].GetString()
                : "spot_bbo";
            spot_aggtrade_shm_name = spot.HasMember("aggtrade_shm_name") && spot["aggtrade_shm_name"].IsString()
                ? spot["aggtrade_shm_name"].GetString()
                : "spot_aggtrade";

        }

        // -------------------------- UBase（Future）配置 --------------------------
        if (m_json_config.HasMember("futures") && m_json_config["futures"].IsObject()) {
            const auto& fut = m_json_config["futures"];
            // 仅解析UBase交易对（删除CBase）
            if (fut.HasMember("ubase_symbols") && fut["ubase_symbols"].IsArray()) {
                for (const auto& v : fut["ubase_symbols"].GetArray()) {
                    if (v.IsString()) ubase_symbols.emplace_back(v.GetString());
                }
            }

            // 仅解析UBase连接数量（删除CBase_conn_count）
            m_ubase_conn_count = fut.HasMember("ubase_conn_count") && fut["ubase_conn_count"].IsInt()
                ? fut["ubase_conn_count"].GetInt()
                : 1;

            // 解析UBase共享内存名称
            fut_bbo_shm_name = fut.HasMember("bbo_shm_name") && fut["bbo_shm_name"].IsString()
                ? fut["bbo_shm_name"].GetString()
                : "future_ubase_bbo";

            fut_aggtrade_shm_name = fut.HasMember("aggtrade_shm_name") && fut["aggtrade_shm_name"].IsString()
                ? fut["aggtrade_shm_name"].GetString()
                : "future_ubase_agg";

        }
        printf("done with config loading \n");

        // 共享内存大小和延迟打印间隔
        md_size = m_json_config.HasMember("md_size") && m_json_config["md_size"].IsInt()
            ? m_json_config["md_size"].GetInt()
            : 100000;
        printf("md_size: %d \n", md_size);
        m_latency_interval_ms = m_json_config.HasMember("latency_print_interval_ms") && m_json_config["latency_print_interval_ms"].IsInt()
            ? m_json_config["latency_print_interval_ms"].GetInt()
            : 5000;

        printf("latency_print_interval_ms: %d \n", m_latency_interval_ms);

        // 打印解析结果（仅Spot+UBase）
        printf("配置解析完成...\n");

    }

    // #######################################################################
    // 共享内存初始化：仅Spot和UBase（删除CBase）
    // #######################################################################
    void init_shm() {
        // -------------------------- Spot共享内存 --------------------------
        if (!spot_symbols.empty()) {
            auto upper_spot = vector_upper(spot_symbols);
            // Spot bookTicker共享内存
            m_spot_bbo_shm = std::make_unique<shm_md_plus<Bookticker>>();
            auto spot_bbo_ptr = m_spot_bbo_shm->create_shm_ptr(spot_bbo_shm_name, upper_spot.size(), md_size, true);
            m_spot_bbo_shm->set_shm_addr((md_shm_head_plus*)spot_bbo_ptr, upper_spot, true);

            // Spot aggTrade共享内存
            m_spot_agg_shm = std::make_unique<shm_md_plus<AggTrade>>();
            auto spot_agg_ptr = m_spot_agg_shm->create_shm_ptr(spot_aggtrade_shm_name, upper_spot.size(), md_size, true);
            m_spot_agg_shm->set_shm_addr((md_shm_head_plus*)spot_agg_ptr, upper_spot, true);
        }

        // -------------------------- UBase（Future）共享内存 --------------------------
        if (!ubase_symbols.empty()) {
            auto upper_ubase = vector_upper(ubase_symbols);
            // UBase bookTicker共享内存
            m_fut_ubase_bbo_shm = std::make_unique<shm_md_plus<Bookticker>>();
            auto fut_bbo_ptr = m_fut_ubase_bbo_shm->create_shm_ptr(fut_bbo_shm_name, upper_ubase.size(), md_size, true);
            m_fut_ubase_bbo_shm->set_shm_addr((md_shm_head_plus*)fut_bbo_ptr, upper_ubase, true);

            // UBase aggTrade共享内存
            m_fut_ubase_agg_shm = std::make_unique<shm_md_plus<AggTrade>>();
            auto fut_agg_ptr = m_fut_ubase_agg_shm->create_shm_ptr(fut_aggtrade_shm_name, upper_ubase.size(), md_size, true);
            m_fut_ubase_agg_shm->set_shm_addr((md_shm_head_plus*)fut_agg_ptr, upper_ubase, true);
        }
    }

    // #######################################################################
    // Spot连接创建（无修改）
    // #######################################################################
    void create_spot_connections() {
        if (spot_symbols.empty() || m_spot_conn_count <= 0) return;

        for (int i = 0; i < m_spot_conn_count; ++i) {
            int conn_id = i + 1; // Spot连接ID：1,2,3...
            std::string host = BINANCE_WS_HOST;

            init_conn_stats(conn_id, "Spot"); // 初始化统计

            // 创建WebSocket连接
            auto conn = create_single_ws_conn(
                host, 
                conn_id, 
                std::bind(&Binance_MD::on_spot_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
            );
            if (conn) {
                m_spot_conns.push_back(conn);
                subscribe_symbols(conn, spot_symbols, conn_id); // 订阅Spot所有交易对
            }
        }
        printf("spot connections done \n");
    }

    // #######################################################################
    // UBase（Future）连接创建（无修改，仅保留）
    // #######################################################################
    void create_ubase_connections() {
        if (ubase_symbols.empty() || m_ubase_conn_count <= 0) return;

        for (int i = 0; i < m_ubase_conn_count; ++i) {
            int conn_id = i + 100; // UBase连接ID：100,101...（避免与Spot冲突）
            std::string host = BINANCE_WS_UB_HOST;

            init_conn_stats(conn_id, "UBase(Future)"); // 初始化统计

            // 创建WebSocket连接
            auto conn = create_single_ws_conn(
                host, 
                conn_id, 
                std::bind(&Binance_MD::on_ubase_md, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
            );
            if (conn) {
                m_ubase_conns.push_back(conn);
                subscribe_symbols(conn, ubase_symbols, conn_id); // 订阅UBase所有交易对
            }
        }
        printf("future connections done \n");
    }

    // #######################################################################
    // 单个WebSocket连接创建（无修改，通用逻辑）
    // #######################################################################
    std::shared_ptr<ws_ssl_client> create_single_ws_conn(
        const std::string& host, 
        int conn_id, 
        const std::function<bool(char*, size_t, int)>& data_cb
    ) {
        try {
            ws_client_callback cb;
            cb.on_closed = [this, conn_id](std::string& msg) {
                logWarn("连接{}已关闭: {}", conn_id, msg);
            };
            cb.on_conn = [this, conn_id](int id) {
                logInfo("连接{}成功建立", conn_id);
            };
            cb.on_data_ready = data_cb;
            cb.on_reconn = [this, conn_id](std::string& msg) {
                logWarn("连接{}正在重连: {}", conn_id, msg);
                reset_conn_stats(conn_id); // 重连后重置统计
            };

            // 创建WS客户端
            auto conn = std::make_shared<ws_ssl_client>(conn_id, host.c_str(), "443", cb);
            conn->connect("/ws");
            conn->start_work(cpu_cores);

            // 等待连接成功（最多重试30次）
            int try_num = 0;
            while (try_num < 30) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (conn->is_ready()) {
                    logInfo("连接{}成功连接到{}", conn_id, host);
                    return conn;
                }
                try_num++;
            }

            logError("连接{}连接{}失败（超过最大重试次数）", conn_id, host);
            return nullptr;
        } catch (const std::exception& e) {
            logError("连接{}创建失败: {}", conn_id, e.what());
            return nullptr;
        }
    }

    // #######################################################################
    // 订阅交易对（无修改，通用逻辑）
    // #######################################################################
    void subscribe_symbols(
        std::shared_ptr<ws_ssl_client> conn, 
        const std::vector<std::string>& symbols, 
        int conn_id
    ) {
        if (!conn || symbols.empty()) return;

        // 订阅aggTrade和bookTicker
        std::string agg_msg = build_subscribe_msg(symbols, "@aggTrade", conn_id);
        std::string book_msg = build_subscribe_msg(symbols, "@bookTicker", conn_id);

        conn->send(agg_msg);
        conn->send(book_msg);
        logInfo("连接{}已订阅{}个交易对（aggTrade + bookTicker）", conn_id, symbols.size());
    }

    // #######################################################################
    // 构建订阅消息（替换fmt库，用stringstream避免依赖）
    // #######################################################################
    std::string build_subscribe_msg(const std::vector<std::string>& symbols, const std::string& suffix, int conn_id) {
        std::string params = "[";
        for (size_t i = 0; i < symbols.size(); ++i) {
            if (i > 0) params += ",";
            params += "\"" + symbols[i] + suffix + "\"";
        }
        params += "]";

        // 用stringstream拼接JSON，避免依赖fmt库
        std::stringstream ss;
        ss << "{\"method\":\"SUBSCRIBE\",\"id\":" << conn_id << ",\"params\":" << params << "}";
        return ss.str();
    }

    // #######################################################################
    // Spot数据回调（无修改）
    // #######################################################################
    // bool on_spot_md(char* data, size_t len, int conn_id) {
    //     Document doc;
    //     doc.Parse(data, len);
    //     if(doc.HasMember("u")){
    //         printf("[spot md] id=%d, %s\n", conn_id, data);
    //         //parse_spot_bookTicker(doc);
    //     }
	// 	else {
    //         printf("[spot agg] %s \n", data);
    //     }

    //     return 1;
    // }

    bool on_spot_md(char* data, size_t len, int conn_id) {
        
        rapidjson::Document doc;
        doc.Parse(data, len);
        if (doc.HasMember("e")) {
            std::string ev = std::string(doc["e"].GetString());
            if (ev == "aggTrade") {
                // 计算延迟
                uint64_t event_time_ms = doc["E"].GetUint64();
                uint64_t event_time_us = event_time_ms * 1000;
                uint64_t local_time_us = get_current_ms_epoch() * 1000;
                uint64_t latency = local_time_us - event_time_us;

                // 更新统计
                update_latency_stats(conn_id, latency);
                parse_spot_aggtrade(doc);
            }
            // else if (ev == "bookTicker") {
            //     parse_spot_bookTicker(doc);
            // }

        } else {
            if (doc.HasMember("b") && doc.HasMember("B")) {
                parse_spot_bookTicker(doc);
            }
        }
        return true;
    }

    // #######################################################################
    // UBase（Future）数据回调（无修改，仅保留）
    // #######################################################################
    // bool on_ubase_md(char* data, size_t len, int conn_id) {
    //     printf("on_ubase_md: conn_id=%d, len=%zu\n", conn_id, len);
    //     return 1;
    // }

    bool on_ubase_md(char* data, size_t len, int conn_id) {
        rapidjson::Document doc;
        doc.Parse(data, len);
        if (doc.HasMember("e")) {
            std::string ev = std::string(doc["e"].GetString());
            if (ev == "aggTrade") {
                // 计算延迟
                uint64_t event_time_ms = doc["E"].GetUint64();
                uint64_t event_time_us = event_time_ms * 1000;
                uint64_t local_time_us = get_current_ms_epoch() * 1000;
                uint64_t latency = local_time_us - event_time_us;

                // 更新统计
                update_latency_stats(conn_id, latency);
                parse_ubase_aggtrade(doc);
            }
            else if (ev == "bookTicker") {
                // 计算延迟
                uint64_t event_time_ms = doc["E"].GetUint64();
                uint64_t event_time_us = event_time_ms * 1000;
                uint64_t local_time_us = get_current_ms_epoch() * 1000;
                uint64_t latency = local_time_us - event_time_us;

                // 更新统计
                update_latency_stats(conn_id, latency);
                parse_ubase_bookTicker(doc);
            }

        } else {
            logInfo("msg received {}", data);
        }
        return true;
    }

    // 现货期货数据格式不一样，先不规范化

    void parse_spot_bookTicker(rapidjson::Document &doc){
        uint64_t updateid = doc["u"].GetUint64();
        if (updateid < m_spot_bbo_update_id) {
            return;
        } else {
            m_spot_bbo_update_id = updateid;
        }

        Bookticker ticker;
		ticker.m_type = ProductType::SPOT;
		// ticker.exchange = ShortExchange::BINANCE;
        // ticker.exch

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(ticker.m_symbol, symbol.data(), sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = updateid;
		ticker.m_LocalTime_us = get_current_ms_epoch()*1000;
        // ticker.m_eventTimestamp = doc["E"].GetUint64();
        // ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = atof(doc["a"].GetString());
        ticker.m_askVol = atof(doc["A"].GetString());
        ticker.m_bidPrice = atof(doc["b"].GetString());
        ticker.m_bidVol = atof(doc["B"].GetString());

		//printf("on spot bookticker md:%s\n",ticker.symbol);
        if(m_spot_bbo_shm != nullptr){
            bool res = m_spot_bbo_shm->on_rtn_data(ticker.m_symbol,ticker);
            // logInfo("insert data, res={}, symbol={}", res, ticker.m_symbol);
        }
        printf("future connections done \n");
    }

    void parse_ubase_bookTicker(rapidjson::Document &doc){
        uint64_t updateid = doc["u"].GetUint64();
        if (updateid < m_fut_bbo_update_id) {
            return;
        } else {
            m_fut_bbo_update_id = updateid;
        }

        Bookticker ticker;
		ticker.m_type = ProductType::UMARGIN;
		// ticker.exch = K4Exchange::BINANCE;
        
		auto symbol = std::string(doc["s"].GetString());
        std::strncpy(ticker.m_symbol, symbol.data(), sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = updateid;
		ticker.m_LocalTime_us = get_current_ms_epoch() * 1000;
        ticker.m_eventTimestamp = doc["E"].GetUint64();
        ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = atof(doc["a"].GetString());
        ticker.m_askVol = atof(doc["A"].GetString());
        ticker.m_bidPrice = atof(doc["b"].GetString());
        ticker.m_bidVol = atof(doc["B"].GetString());

        if(m_fut_ubase_bbo_shm != nullptr){
			//std::cout<<"wr md:"<<ticker.symbol<<",ask px:"<<ticker.ask_price<<std::endl;
            bool ret = m_fut_ubase_bbo_shm->on_rtn_data(ticker.m_symbol,ticker);
            // logInfo("insert data, res={}, symbol={}", ret, ticker.m_symbol);
			if(!ret)
				std::cout<<"wr ubase fail:"<<ticker.m_symbol<<std::endl;
        }
    }

    void parse_spot_aggtrade(rapidjson::Document &doc) {
        uint64_t updateid = doc["a"].GetUint64();
        if (updateid < m_spot_agg_update_id) {
            return;
        } else {
            m_spot_agg_update_id = updateid;
        }

        AggTrade aggdata;
        aggdata.m_type = ProductType::SPOT;

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(aggdata.m_symbol, symbol.data(), sizeof(aggdata.m_symbol) - 1);
        aggdata.m_aggTradeId = updateid;
        aggdata.m_firstTradeId = doc["f"].GetUint64();
        aggdata.m_lastTradeId = doc["l"].GetUint64();
        aggdata.m_price = atof(doc["p"].GetString());
        aggdata.m_vol = atof(doc["q"].GetString());
        aggdata.m_isBuy = doc["m"].GetBool();
        aggdata.m_eventTimestamp = doc["E"].GetUint64();
        aggdata.m_tradeTimestamp = doc["T"].GetUint64();
        aggdata.m_LocalTime_us = get_current_ms_epoch() * 1000;

        if (m_spot_agg_shm != nullptr) {
            bool res = m_spot_agg_shm->on_rtn_data(aggdata.m_symbol, aggdata);
            // logInfo("insert agg data, res={}, symbol={}", res, aggdata.m_symbol);
        }
        params += "]";

        // 用stringstream拼接JSON，避免依赖fmt库
        std::stringstream ss;
        ss << "{\"method\":\"SUBSCRIBE\",\"id\":" << conn_id << ",\"params\":" << params << "}";
        return ss.str();
    }

    void parse_ubase_aggtrade(rapidjson::Document &doc) {
        uint64_t updateid = doc["a"].GetUint64();
        if (updateid < m_fut_agg_update_id) {
            return;
        } else {
            m_fut_agg_update_id = updateid;
        }

        AggTrade aggdata;
        aggdata.m_type = ProductType::UMARGIN;

        auto symbol = std::string(doc["s"].GetString());
        std::strncpy(aggdata.m_symbol, symbol.data(), sizeof(aggdata.m_symbol) - 1);
        aggdata.m_aggTradeId = updateid;
        aggdata.m_firstTradeId = doc["f"].GetUint64();
        aggdata.m_lastTradeId = doc["l"].GetUint64();
        aggdata.m_price = atof(doc["p"].GetString());
        aggdata.m_vol = atof(doc["q"].GetString());
        aggdata.m_isBuy = doc["m"].GetBool();
        aggdata.m_eventTimestamp = doc["E"].GetUint64();
        aggdata.m_tradeTimestamp = doc["T"].GetUint64();
        aggdata.m_LocalTime_us = get_current_ms_epoch() * 1000;

        if (m_fut_ubase_agg_shm != nullptr) {
            bool res = m_fut_ubase_agg_shm->on_rtn_data(aggdata.m_symbol, aggdata);
            // logInfo("insert agg data, res={}, symbol={}", res, aggdata.m_symbol);
        }
		else {
            printf("[spot agg] %s \n", data);
        }

        return 1;
    }

    bool process_md_data(
        char* data, size_t len, int conn_id, 
        ProductType type,
        const std::function<void(Document&)>& book_parser,
        const std::function<void(Document&)>& agg_parser
    ) {
        Document doc;
        doc.Parse(data, len);

        printf("[mddata] %s\n", data);
        if (!doc.IsObject() || !doc.HasMember("e") || !doc.HasMember("E")) {
            logError("连接{}: 无效数据（缺少事件类型或时间戳）", conn_id);
            return false;
        }

        // 计算延迟
        uint64_t event_time_ms = doc["E"].GetUint64();
        uint64_t event_time_us = event_time_ms * 1000;
        uint64_t local_time_us = get_current_ms_epoch() * 1000;
        uint64_t latency = local_time_us - event_time_us;

        // 更新统计
        update_latency_stats(conn_id, latency);

        // 解析行情
        std::string ev = doc["e"].GetString();
        if (ev == "bookTicker") {
            book_parser(doc);
        } else if (ev == "aggTrade") {
            agg_parser(doc);
        }

        return true;
    }

    // #######################################################################
    // 延迟统计相关（删除CBase打印，仅保留Spot/UBase）
    // #######################################################################
    void init_conn_stats(int conn_id, const std::string& product_type) {
        std::lock_guard<std::mutex> lock(m_stats_mutex);

        g_conn_latency_stats.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(conn_id),
            std::forward_as_tuple(conn_id, product_type) // 调用带参构造
        );
        logInfo("连接{}统计初始化完成（类型：{}）", conn_id, product_type);
    }

    void reset_conn_stats(int conn_id) {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        auto it = g_conn_latency_stats.find(conn_id);
        if (it != g_conn_latency_stats.end()) {
            it->second.total_count = 0;
            it->second.total_latency = 0;
            it->second.min_latency = UINT64_MAX;
            logInfo("连接{}: 重置延迟统计", conn_id);
        }
    }

    void update_latency_stats(int conn_id, uint64_t latency) {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        auto it = g_conn_latency_stats.find(conn_id);
        if (it != g_conn_latency_stats.end()) {
            it->second.total_count++;
            it->second.total_latency += latency;
            it->second.latest_latency = latency;
            if (latency < it->second.min_latency) {
                it->second.min_latency = latency;
            }
        }
    }

    // 启动延迟打印线程（仅打印Spot/UBase）
    void start_latency_print_timer() {
        m_print_thread = std::thread([this]() {
            if (!cpu_cores.empty()) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);

                for (int core : cpu_cores) {
                    if (core >= 0 && core < CPU_SETSIZE) {
                        CPU_SET(core, &cpuset);
                        // printf("Adding core %d to affinity set\n", core);
                    } else {
                        // printf("Invalid core id: %d (ignored)\n", core);
                    }
                }

                int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                if (ret != 0) {
                    // perror("pthread_setaffinity_np failed");
                } else {
                    // printf("Thread bound to %zu cores\n", cpu_cores.size());
                }
            }

            while (m_print_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(m_latency_interval_ms));
                print_latency_stats();
            }
        });
    }

    // 打印延迟统计（删除CBase相关）
    void print_latency_stats() {
        std::lock_guard<std::mutex> lock(m_stats_mutex);
        logInfo("\n==================== 连接速度对比（延迟单位：微秒） ====================");
        print_product_stats("Spot");                // 仅打印Spot
        print_product_stats("UBase(Future)");       // 仅打印UBase（Future）
        logInfo("======================================================================");
    }

    void print_product_stats(const std::string& product) {
        logInfo("{} 连接统计：", product);
        std::vector<ConnLatencyStats> product_conns;

        std::lock_guard<std::mutex> lock(m_stats_mutex); // 确保线程安全
        // 遍历统计容器，筛选目标产品类型的连接
        for (auto& [id, stats] : g_conn_latency_stats) { // 注意：此处用非const引用&，允许移动
            if (stats.product_type == product) {
                // 关键：用移动语义插入vector，避免拷贝
                product_conns.emplace_back(std::move(stats));
            }
        }

        // 排序（此时vector中的元素是移动过来的，可正常排序）
        std::sort(product_conns.begin(), product_conns.end(),
            [](const ConnLatencyStats& a, const ConnLatencyStats& b) {
                return a.get_avg_latency() < b.get_avg_latency();
            });

        // 打印统计（原子变量需用load()读取）
        for (const auto& stats : product_conns) {
            logInfo("  连接{}: 平均={:.1f} | 最新={} | 最小={} | 条数={}",
                stats.conn_id,
                stats.get_avg_latency(),
                stats.latest_latency.load(std::memory_order_relaxed),
                stats.min_latency.load(std::memory_order_relaxed),
                stats.total_count.load(std::memory_order_relaxed));
        }

        if (!product_conns.empty()) {
            const auto& fastest = product_conns[0];
            logInfo("  {}最快连接：连接{}（平均延迟{:.1f}微秒）",
                product, fastest.conn_id, fastest.get_avg_latency());
        }
    }
    
    // #######################################################################
    // 停止连接（仅Spot和UBase）
    // #######################################################################
    void stop_all_connections() {
        // 停止Spot连接（改用close()，替换为实际公共停止方法）
        for (auto& conn : m_spot_conns) {
            if (conn) {
                conn->close(); // 关键：替换stop()为ws_ssl_client的公共停止方法
                logInfo("Spot连接已关闭"); // 假设get_id()获取连接ID
            }
        }
        // 停止UBase连接
        for (auto& conn : m_ubase_conns) {
            if (conn) {
                conn->close(); // 同样改用公共停止方法
                logInfo("UBase连接已关闭");
            }
        }
        logInfo("Spot和UBase连接已全部停止");
    }

    // #######################################################################
    // 工具函数（无修改）
    // #######################################################################
    std::string to_uppercase(const std::string& str) {
        std::string res = str;
        boost::to_upper(res);
        return res;
    }

    std::vector<std::string> vector_upper(const std::vector<std::string>& input) {
        std::vector<std::string> res;
        for (const auto& s : input) {
            res.push_back(to_uppercase(s));
        }
        return res;
    }

    // // #######################################################################
    // // Spot行情解析（无修改）
    // // #######################################################################
    // void parse_spot_bookTicker(Document& doc) {
    //     if (!m_spot_bbo_shm) return;
    //     Bookticker ticker;
    //     ticker.m_type = ProductType::SPOT;
    //     fill_bookticker_data(ticker, doc);
    //     m_spot_bbo_shm->on_rtn_data(ticker.m_symbol, ticker);
    // }

    // void parse_spot_aggtrade(Document& doc) {
    //     if (!m_spot_agg_shm) return;
    //     AggTrade agg;
    //     agg.m_type = ProductType::SPOT;
    //     fill_aggtrade_data(agg, doc);
    //     m_spot_agg_shm->on_rtn_data(agg.m_symbol, agg);
    // }

    // // #######################################################################
    // // UBase（Future）行情解析（无修改，仅保留）
    // // #######################################################################
    // void parse_ubase_bookTicker(Document& doc) {
    //     if (!m_fut_ubase_bbo_shm) return;
    //     Bookticker ticker;
    //     ticker.m_type = ProductType::UMARGIN;
    //     fill_bookticker_data(ticker, doc);
    //     m_fut_ubase_bbo_shm->on_rtn_data(ticker.m_symbol, ticker);
    // }

    // void parse_ubase_aggtrade(Document& doc) {
    //     if (!m_fut_ubase_agg_shm) return;
    //     AggTrade agg;
    //     agg.m_type = ProductType::UMARGIN;
    //     fill_aggtrade_data(agg, doc);
    //     m_fut_ubase_agg_shm->on_rtn_data(agg.m_symbol, agg);
    // }

    // #######################################################################
    // 行情数据填充（无修改，通用逻辑）
    // #######################################################################
    void fill_bookticker_data(Bookticker& ticker, Document& doc) {
        auto symbol = doc["s"].GetString();
        std::strncpy(ticker.m_symbol, symbol, sizeof(ticker.m_symbol) - 1);
        ticker.m_updateId = doc["u"].GetUint64();
        ticker.m_LocalTime_us = get_current_ms_epoch() * 1000;
        ticker.m_eventTimestamp = doc["E"].GetUint64();
        ticker.m_tradeTimestamp = doc["T"].GetUint64();
        ticker.m_askPrice = std::stod(doc["a"].GetString());
        ticker.m_askVol = std::stod(doc["A"].GetString());
        ticker.m_bidPrice = std::stod(doc["b"].GetString());
        ticker.m_bidVol = std::stod(doc["B"].GetString());
    }

    void fill_aggtrade_data(AggTrade& agg, Document& doc) {
        auto symbol = doc["s"].GetString();
        std::strncpy(agg.m_symbol, symbol, sizeof(agg.m_symbol) - 1);
        agg.m_aggTradeId = doc["a"].GetUint64();
        agg.m_firstTradeId = doc["f"].GetUint64();
        agg.m_lastTradeId = doc["l"].GetUint64();
        agg.m_price = std::stod(doc["p"].GetString());
        agg.m_vol = std::stod(doc["q"].GetString());
        agg.m_isBuy = doc["m"].GetBool();
        agg.m_eventTimestamp = doc["E"].GetUint64();
        agg.m_tradeTimestamp = doc["T"].GetUint64();
        agg.m_LocalTime_us = get_current_ms_epoch() * 1000;
    }

private:
    // #######################################################################
    // 成员变量：删除CBase相关，仅保留Spot和UBase
    // #######################################################################
    Document& m_json_config;               // 配置文件JSON
    bool m_bDoneConnect = false;           // 连接完成标志
    std::atomic<bool> m_print_running;     // 延迟打印线程控制

    // 连接数量配置
    int m_spot_conn_count = 1;             // Spot连接数
    int m_ubase_conn_count = 1;            // UBase（Future）连接数
    int m_latency_interval_ms = 5000;      // 延迟打印间隔（ms）

    // 交易对列表
    std::vector<std::string> spot_symbols;    // Spot交易对
    std::vector<std::string> ubase_symbols;   // UBase（Future）交易对

    // 共享内存名称
    std::string spot_bbo_shm_name;        // Spot bookTicker共享内存名
    std::string spot_aggtrade_shm_name;   // Spot aggTrade共享内存名
    std::string fut_bbo_shm_name;         // UBase bookTicker共享内存名
    std::string fut_aggtrade_shm_name;    // UBase aggTrade共享内存名
    int md_size = 100000;                 // 共享内存大小

    // 多个返回 记载各个数据最新update id
    uint64_t m_spot_bbo_update_id;
    uint64_t m_spot_agg_update_id;
    uint64_t m_fut_bbo_update_id;
    uint64_t m_fut_agg_update_id;

    // WebSocket连接容器
    std::vector<std::shared_ptr<ws_ssl_client>> m_spot_conns;   // Spot连接
    std::vector<std::shared_ptr<ws_ssl_client>> m_ubase_conns;  // UBase连接

    // 共享内存智能指针
    std::unique_ptr<shm_md_plus<Bookticker>> m_spot_bbo_shm;    // Spot bookTicker
    std::unique_ptr<shm_md_plus<AggTrade>> m_spot_agg_shm;      // Spot aggTrade
    std::unique_ptr<shm_md_plus<Bookticker>> m_fut_ubase_bbo_shm;// UBase bookTicker
    std::unique_ptr<shm_md_plus<AggTrade>> m_fut_ubase_agg_shm;  // UBase aggTrade

    // 线程与同步
    std::thread m_print_thread;           // 延迟打印线程
    std::mutex m_stats_mutex;             // 统计数据互斥锁
};

// ###########################################################################
// 导出创建实例函数（供Runner调用，无修改）
// ###########################################################################
extern "C" std::unique_ptr<RT_CryptoMDBase> create_mdlib(rapidjson::Document& json_config) {
    return std::make_unique<Binance_MD>(json_config);
}