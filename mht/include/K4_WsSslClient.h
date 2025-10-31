#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <set>
#include <thread>
#include <chrono>
#include <queue>
#include <iostream>
#include <mutex>
#include <deque>

#include <boost/asio/io_context.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>

namespace mht_rt {
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

typedef struct _ws_client_callback {
	std::function<void(std::string& msg)> on_reconn;
	std::function<void(int id)> on_conn;
	std::function<void(std::string& msg)> on_closed;
	std::function<bool(char* data, size_t len, int id)> on_data_ready;
} ws_client_callback;

template<typename WSType>
class ws_client_base : public std::enable_shared_from_this<ws_client_base<WSType>> {
public:
	ws_client_base(int id, const std::string& host, const std::string& port, ws_client_callback callback)
		: resolver_(net::make_strand(ioctx_)), host_(host), port_(port), id_(id), m_callback(callback) {}

	virtual ~ws_client_base() {
		close();
	}

	virtual void connect(const std::string& uri) = 0;
	virtual void on_resolved(boost::beast::error_code ec, boost::asio::ip::tcp::resolver::results_type res) = 0;
	virtual void on_connected(boost::beast::error_code ec) = 0;


	virtual void close() {
		try {
			stop_ = true;
			if (ws_) ws_->close(websocket::close_code::normal);
		} catch (...) {}
	}

    virtual void disconnect() {
        close();
        if (m_work.joinable()) {
            m_work.join();
        }
        ioctx_.stop();
    }

	virtual void re_connect() {
		ready_ = false;
		std::string msg = uri_ + " ready to Reconn";
		m_callback.on_reconn(msg);
		if (!stop_) {
			msg = uri_ + " has been stop,ready to close";
			m_callback.on_reconn(msg);
			close();
		}
		std::this_thread::sleep_for(std::chrono::seconds(2));
		msg = uri_ + " exec Reconning....";
		m_callback.on_reconn(msg);
		connect(uri_);
	}

	void start_read() {
		async_read_data();
	}

	void send(const std::string& data) {
		if (!ready_ || stop_) return;

		{
			std::lock_guard<std::mutex> lock(m_send_mutex);
			m_send_queue.push_back(data);
			if (m_write_in_progress) return;
			m_write_in_progress = true;
		}

		do_write();
	}

	void start_work(const std::vector<int>& cpu_cores = {}) {
    if (!m_work.joinable()) {
        m_work = std::thread([this, cpu_cores]() {
            if (!cpu_cores.empty()) {
                cpu_set_t cpuset;
                CPU_ZERO(&cpuset);

                for (int core : cpu_cores) {
                    if (core >= 0 && core < CPU_SETSIZE) {
                        CPU_SET(core, &cpuset);
                        printf("Adding core %d to affinity set\n", core);
                    } else {
                        printf("Invalid core id: %d (ignored)\n", core);
                    }
                }

                int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
                if (ret != 0) {
                    perror("pthread_setaffinity_np failed");
                } else {
                    printf("Thread bound to %zu cores\n", cpu_cores.size());
                }
            }
            work();  // 启动核心逻辑
        });
    }
}

	void work() {
		ioctx_.run();
	}

	bool is_ready() { return ready_; }

protected:
	void on_ready(boost::beast::error_code ec) {
		if (ec) {
			printf("ws_client_base::on_ready [%d] fail to process handshake for host %s,err message:%s\n", id_, host_.c_str(), ec.message().c_str());
			re_connect();
			return;
		}
		printf("ws_client_base::on_ready, websocket connection to %s:%s succeeded\n", host_.c_str(), port_.c_str());
		m_callback.on_conn(id_);
		ready_ = true;
		start_read();
	}

	void async_read_data() {
		ws_->async_read(buffer_, std::bind(&ws_client_base::on_read, this->shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	}

	void on_write(boost::beast::error_code ec, std::size_t) {
		{
			std::lock_guard<std::mutex> lock(m_send_mutex);
			if (!m_send_queue.empty()) m_send_queue.pop_front();
		}

		if (ec) {
			printf("ws_client_base::on_write [%d]:%s write error, err message:%s\n", id_, host_.c_str(), ec.message().c_str());
			{
				std::lock_guard<std::mutex> lock(m_send_mutex);
				m_write_in_progress = false;
			}
			if (!stop_) re_connect();
			return;
		}

		do_write();
	}

	void do_write() {
		std::string data;
		{
			std::lock_guard<std::mutex> lock(m_send_mutex);
			if (m_send_queue.empty() || !ready_) {
				m_write_in_progress = false;
				return;
			}
			data = std::move(m_send_queue.front());
		}

		ws_->async_write(net::buffer(data),
			std::bind(&ws_client_base::on_write, this->shared_from_this(), std::placeholders::_1, std::placeholders::_2));
	}

	void on_read(boost::beast::error_code ec, std::size_t recv_len) {
		if (ec) {
			printf("ws_client_base::on_read [%d]:%s read error: %s\n", id_, host_.c_str(), ec.message().c_str());
			if (!stop_) re_connect();
			return;
		}

		char* buf = new char[recv_len + 1]();
		int total_len = 0;
		for (const auto& it : buffer_.data()) {
			auto data_size = it.size();
			memcpy(buf + total_len, static_cast<const char*>(it.data()), data_size);
			total_len += data_size;
		}
		buffer_.consume(buffer_.size());

		bool ok = m_callback.on_data_ready(buf, total_len, id_);
		delete[] buf;
		if (!ok) {
			printf("process data failed\n");
		}
		if (!stop_) {
			async_read_data();
		}
	}

	void on_close(boost::system::error_code ec) {
		if (ec) {
			printf("fail to close ws,%s\n", ec.message().c_str());
		}
		m_callback.on_closed(uri_);
		stop_ = true;
	}

	template <typename Derived>
	std::shared_ptr<Derived> shared_from_base() {
		return std::static_pointer_cast<Derived>(this->shared_from_this());
	}

protected:
	int id_;
	net::io_context ioctx_;
	tcp::resolver resolver_;
	std::shared_ptr<websocket::stream<WSType>> ws_;
	std::string host_;
	std::string port_;
	std::string uri_;
	std::thread m_work;
	beast::multi_buffer buffer_;
	bool stop_ = true;
	bool ready_ = false;

	ws_client_callback m_callback;
	std::mutex m_send_mutex;
	std::deque<std::string> m_send_queue;
	bool m_write_in_progress = false;
};

class ws_ssl_client : public ws_client_base<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> {
public:
	ws_ssl_client(int id, const std::string& host, const std::string& port, ws_client_callback callback)
		: ws_client_base(id, host, port, callback), ssl_{ ssl::context::sslv23_client } {
		ws_ = std::make_shared<websocket::stream<ssl::stream<tcp::socket>>>(net::make_strand(ioctx_), ssl_);
	}

	~ws_ssl_client() {
		close();
	}

	void reset_uri_(std::string& uri) {
		uri_ = uri;
	}

	void connect(const std::string& uri) override {
		uri_ = uri.empty() ? "/" : uri;
		resolver_.async_resolve(host_, port_,
			std::bind(&ws_ssl_client::on_resolved, shared_from_base<ws_ssl_client>(),
				std::placeholders::_1, std::placeholders::_2));
	}

	void re_connect() override {
		ws_ = std::make_shared<websocket::stream<ssl::stream<tcp::socket>>>(net::make_strand(ioctx_), ssl_);
		ws_client_base::re_connect();
	}

private:
	void on_resolved(beast::error_code ec, tcp::resolver::results_type res) override {
		if (ec) {
			printf("on_resolved [%d] fail to resolve host %s,err message:%s\n", id_, host_.c_str(), ec.message().c_str());
			re_connect();
			return;
		}

		for (auto const& entry : res) {
			auto ep = entry.endpoint();
			std::cout << "[Session " << id_ << "] resolved LB IP: "
					<< ep.address().to_string() << ":" << ep.port() << std::endl;
		}

		if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
			printf("failed to set host name\n");
			re_connect();
			return;
		}

		net::async_connect(ws_->next_layer().next_layer(), res.begin(), res.end(),
			std::bind(&ws_ssl_client::on_connected, shared_from_base<ws_ssl_client>(), std::placeholders::_1));
	}

	void on_connected(beast::error_code ec) override {
		if (ec) {
			printf("on_connected [%d]fail to connect to host %s,err message:%s\n", id_, host_.c_str(), ec.message().c_str());
			re_connect();
			return;
		}

		try {
			auto remote_ep = ws_->next_layer().next_layer().remote_endpoint();
			std::cout << "[Session " << id_ << "] connected to real WS server: "
					<< remote_ep.address().to_string() << ":" << remote_ep.port() << std::endl;
		} catch (std::exception& e) {
			std::cerr << "Failed to get remote endpoint: " << e.what() << std::endl;
		}

		ws_->control_callback(
			[this](websocket::frame_type kind, beast::string_view str) {
				if(kind==websocket::frame_type::ping){
					//std::cout<<"rcv ping and rsp pong\n";
					ws_->async_pong({}, [](beast::error_code) {});
				}
			});

		stop_ = false;
		ws_->next_layer().async_handshake(ssl::stream_base::client,
			std::bind(&ws_ssl_client::on_ssl_handshake, shared_from_base<ws_ssl_client>(), std::placeholders::_1));
	}

	void on_ssl_handshake(beast::error_code ec) {
		ws_->async_handshake(host_, uri_, std::bind(&ws_ssl_client::on_ready,
			shared_from_base<ws_ssl_client>(), std::placeholders::_1));
	}

private:
	ssl::context ssl_;
};

};

