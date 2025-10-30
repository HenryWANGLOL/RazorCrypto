#pragma once
#include <iconv.h>
#include <x86intrin.h>
#include <thread>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <cmath>
#include "RT_ShmUtil.h"
#include <chrono>
#define EPSILON 0.00000001
constexpr double DOUBLEMAX = std::numeric_limits<double>::max();
namespace mht_rt
{
	template <class Facet>
	struct deletable_facet : Facet
	{
		template <class... Args>
		deletable_facet(Args &&...args) : Facet(std::forward<Args>(args)...)
		{
		}

		~deletable_facet()
		{
		}
	};
	typedef deletable_facet<std::codecvt_byname<wchar_t, char, std::mbstate_t>> codec_facet_t;

#ifdef __linux__
#define mm_unlikely(x) __builtin_expect(!!(x), 0)
#define mm_likely(x) __builtin_expect(!!(x), 1)
#else
#define mm_unlikely(x) x
#define mm_likely(x) x
#endif

#define HAS_KEY(C, KEY) (C.find(KEY) != C.end())

	const int INSTRUMENT_ID_LEN = 24;
	const int PRODUCT_ID_LEN = 24;
	const int DATE_LEN = 11;
	const int CURRENCY_LEN = 5;
	const int EXCHANGE_ID_LEN = 16;
	const int ACCOUNT_ID_LEN = 32;
	const int CLIENT_ID_LEN = 32;
	const int EXEC_ID_LEN = 32;
	const int SOURCE_ID_LEN = 16;
	const int BROKER_ID_LEN = 32;
	const int ERROR_MSG_LEN = 81;
	const int FORQUOTESYSID_LEN = 21;

	const int MAX_STRATEGY_NAME_LEN = 128;
	const int MAX_STRATEGY_NUM = 128;
	const int MAX_STRATEGY_DIR_LEN = 256;

	static uint64_t com_hash(const char *security_id, unsigned int len)
	{
		uint64_t ret = 0;
		unsigned i;
		unsigned int seed = 131; // the magic number, 31, 131, 1313, 13131, etc.. orz.
		for (i = 0; i < len; ++i)
		{
			ret = ret * seed + security_id[i];
		}
		return ret;
	}

	class tb_time_util
	{
	private:
		tb_time_util()
		{
			m_cpu_frp_per_us = 0.0f;
			m_start = 0;
		}
		~tb_time_util() {}

	public:
		static tb_time_util *get_instance()
		{
			static tb_time_util ins;
			return &ins;
		}

		static inline uint64_t rdtscp()
		{
			unsigned int ui;
			uint64_t res = __rdtscp(&ui);
			return res;
		}

		void init()
		{
			com_cpu_fre_per_us();
			m_start = rdtscp();
		}

		inline void check()
		{
			if (m_start == 0)
			{
				init();
			}
		}

		double com_cpu_fre_per_us()
		{
#if defined(_WIN32) || defined(_WIN64)
			LARGE_INTEGER freq, t1, t2;
			QueryPerformanceFrequency(&freq);
			QueryPerformanceCounter(&t1);
			uint64_t r1 = rdtscp();
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			uint64_t r2 = rdtscp();
			QueryPerformanceCounter(&t2);

			double dur_us = (double)(t2.QuadPart - t1.QuadPart) * 1e6 / freq.QuadPart;
			m_cpu_frp_per_us = (r2 - r1) / dur_us;
			return m_cpu_frp_per_us;
#else
			struct timespec ts1, ts2;
			uint64_t r1 = rdtscp();
			clock_gettime(CLOCK_MONOTONIC_RAW, &ts1);
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			uint64_t r2 = rdtscp();
			clock_gettime(CLOCK_MONOTONIC_RAW, &ts2);

			double us1 = ts1.tv_sec * 1e6 + ts1.tv_nsec / 1e3;
			double us2 = ts2.tv_sec * 1e6 + ts2.tv_nsec / 1e3;

			m_cpu_frp_per_us = (r2 - r1) / (us2 - us1);
			return m_cpu_frp_per_us;
#endif
		}

		double get_dur_us(int64_t begin, int64_t end)
		{
			check();
			return abs(end - begin) / m_cpu_frp_per_us;
		}

		double get_dur_us(uint64_t prev)
		{
			check();
			uint64_t now = rdtscp();
			return get_dur_us(prev, now);
		}

		double get_dur_from_start()
		{
			check();
			uint64_t now = rdtscp();
			return get_dur_us(m_start, now);
		}

		double m_cpu_frp_per_us;
		uint64_t m_start;
	};

	struct tp_record
	{
		int index = 0;
		int len = 0;
		uint64_t tp[0];
	};

	static tp_record *create_tp_record(std::string shm_name, int len)
	{
#ifdef __linux__
		std::string md_fn = "/dev/shm/" + shm_name;
		remove(md_fn.data());
#endif
		int shm_len = sizeof(struct tp_record) + sizeof(uint64_t) * len;
		void *ptr = share_memory_util::get_instance()->load_page_buffer(shm_name, shm_len, true, true, true);
		memset(ptr, 0, shm_len);
		tp_record *ret = (tp_record *)ptr;
		ret->index = 0;
		ret->len = len;
		return ret;
	}

	static tp_record *get_tp_record(std::string _shm_name)
	{
		struct stat info;
#ifdef __linux__
		std::string shm_name = "/dev/shm/" + _shm_name;
#else
		std::string shm_name = _shm_name;
#endif
		stat(shm_name.data(), &info);
		size_t _shm_size = info.st_size;

		void *ret = share_memory_util::get_instance()->load_page_buffer(_shm_name, _shm_size, false, true, true);
		return (tp_record *)ret;
	}

	struct cmp_str
	{
		inline bool operator()(char const *a, char const *b)
		{
			return std::strcmp(a, b) < 0;
		}
	};

	struct BKDR_Hash_Compare
	{
		static size_t hash(const char *x)
		{
			unsigned int seed = 131; // the magic number, 31, 131, 1313, 13131, etc.. orz..
			size_t h = 0;
			unsigned char *p = (unsigned char *)x;
			while (*p)
			{
				h = h * seed + (*p++);
			}
			return h;
		}

		static bool equal(const char *x, const char *y)
		{
			return strcmp(x, y) == 0;
		}
	};

	struct MyHashCompare
	{
		static size_t hash(const char *x)
		{
			size_t h = 0;
			for (const char *s = x; *s; ++s)
			{
				h = (h * 17) ^ *s;
			}
			return h;
		}

		static bool equal(const char *x, const char *y)
		{
			return strcmp(x, y) == 0;
		}
	};

	inline bool is_greater(double x, double y)
	{
		return (x - y) > EPSILON;
	}

	inline bool is_less(double x, double y)
	{
		return (x - y) < -EPSILON;
	}

	inline bool is_equal(double x, double y)
	{
		return std::abs(x - y) <= EPSILON * std::abs(x);
	}

	inline bool is_greater_equal(double x, double y)
	{
		return is_greater(x, y) || is_equal(x, y);
	}

	inline bool is_less_equal(double x, double y)
	{
		return is_less(x, y) || is_equal(x, y);
	}

	inline bool is_zero(double x)
	{
		return is_equal(x, 0.0);
	}

	inline bool is_too_large(double x)
	{
		return is_greater(x, DOUBLEMAX);
	}

	inline bool is_valid_price(double price)
	{
		return !is_less_equal(price, 0.0) && !is_too_large(price);
	}

	inline double rounded(double x, int n)
	{
		if (is_too_large(x) || is_zero(x) || is_too_large(std::abs(x)))
		{
			return 0.0;
		}
		else
		{
			char out[64];
			double xrounded;
			sprintf(out, "%.*f", n, x);
			xrounded = strtod(out, 0);
			return xrounded;
		}
	}


	inline std::string find_largest_filename(const std::string& folder_path) {
		std::string largest_filename;
		bool found_file = false;

		DIR* dir = opendir(folder_path.c_str());
		if (dir == nullptr) {
			throw std::runtime_error("无法打开目录: " + folder_path);
		}

		dirent* entry;
		while ((entry = readdir(dir)) != nullptr) {
			// 跳过当前目录(.)和父目录(..)
			if (std::string(entry->d_name) == "." || std::string(entry->d_name) == "..") {
				continue;
			}

			// 构建完整路径
			std::string full_path = folder_path + "/" + entry->d_name;
			
			// 检查是否为文件
			struct stat file_info;
			if (stat(full_path.c_str(), &file_info) == 0 && S_ISREG(file_info.st_mode)) {
				std::string current_filename = entry->d_name;
				
				// 比较文件名，更新最大文件名
				if (!found_file || current_filename > largest_filename) {
					largest_filename = current_filename;
					found_file = true;
				}
			}
		}

		closedir(dir);

		if (!found_file) {
			throw std::runtime_error("目录中没有找到任何文件");
		}

		return largest_filename;
	}


	inline bool string_equals(const std::string &s1, const std::string &s2)
	{
		return std::strcmp(s1.c_str(), s2.c_str()) == 0;
	}

	inline bool string_equals_n(const std::string &s1, const std::string &s2, size_t l)
	{
		return std::strncmp(s1.c_str(), s2.c_str(), l) == 0;
	}

	inline bool endswith(const std::string &str, const std::string &suffix)
	{
		return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
	}

	inline bool startswith(const std::string &str, const std::string &prefix)
	{
		return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
	}

	inline void to_upper(std::string &data)
	{
		std::for_each(data.begin(), data.end(), [](char &c)
					  { c = ::toupper(c); });
	}

	inline std::string to_upper_copy(const std::string &data)
	{
		std::string rtn = data;
		to_upper(rtn);
		return rtn;
	}

	inline void to_lower(std::string &data)
	{
		std::for_each(data.begin(), data.end(), [](char &c)
					  { c = ::tolower(c); });
	}

	inline std::string to_lower_copy(const std::string &data)
	{
		std::string rtn = data;
		to_lower(rtn);
		return rtn;
	}

	inline int GbkToUtf8(char *src, size_t src_len, char *dst, size_t dst_len)
	{
		iconv_t cd = iconv_open("UTF-8", "GBK");
		if (cd == (iconv_t)-1)
		{
			return -1;
		}

		// iconv modifies the input/output pointers
		char *inbuf = src;
		char *outbuf = dst;
		size_t inbytesleft = src_len;
		size_t outbytesleft = dst_len;

		memset(dst, 0, dst_len);

		if (iconv(cd, &inbuf, &inbytesleft, &outbuf, &outbytesleft) == (size_t)-1)
		{
			iconv_close(cd);
			return -1;
		}

		iconv_close(cd);
		return 0;
	}

	inline int _code_2_int(const char* code, int len) {
		int ret1 = 0;
		int ret2 = 0;
		bool is_1st_num = false;
		for (int i = 0; i < len; ++i) {
			if (code[i] >= 'A' && is_1st_num == false) {
				ret1 = ret1 * 100 + (code[i] - 'A') + 10;
			}
			else {
				if (is_1st_num == false) {
					is_1st_num = true;
					if (ret1 < 100)
						ret1 *= 100;
				}
				ret2 = ret2 * 10 + (code[i] - '0');
			}
		}
		return ret1 * 10000 + ret2;
	}

	inline std::string _int_2_code(int encoded_value) {
		if (encoded_value == 0) {
			return "0";
		}
		
		// 分离ret2和ret1部分
		int ret2 = encoded_value % 10000;
		int ret1 = encoded_value / 10000;
		
		std::string result;
		
		// 处理ret1部分（字母部分）
		if (ret1 > 0) {
			// 反向解析字母编码
			int temp = ret1;
			std::string letters;
			
			while (temp > 0) {
				int pair = temp % 100;
				temp /= 100;
				
				// 字母编码范围是10-35（A-Z）
				if (pair >= 10 && pair <= 99) {
					letters = char('A' + (pair - 10)) + letters;
				} 
				// 处理特殊情况：原始函数中ret1<100时会乘以100
				else if (pair == 0) {
					// 可能是填充的0，跳过
					continue;
				}
				else {

					return "ERROR";
				}
			}
			result += letters;
		}
		
		// 处理ret2部分（数字部分）
		if (ret2 > 0) {
			result += std::to_string(ret2);
		}
		
		return result;
	}

	inline std::string code_2_underlying(std::string code) {
		char buf[16] = { 0 };
		int i = -1;
		for (auto& ch : code) {
			if (ch < 'A') {
				return std::string(buf);
			}
			buf[++i] = std::toupper(ch);
		}
		return std::string(buf);
	}


	static int get_cur_date()
	{
		time_t timep;
		time(&timep);
		struct tm tm;
		tm = *localtime(&timep);

		int res;
		res = (tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday;
		return res;
	}

	static int get_cur_tp(int add = 0)
	{
		time_t timep;
		time(&timep);
		timep += add;
		struct tm tm;
		tm = *localtime(&timep);

		int res;
		res = tm.tm_hour * 10000 + tm.tm_min * 100 + tm.tm_sec;
		return res;
	}

	static int64_t get_cur_sec()
	{
		time_t timep;
		time(&timep);
		return timep;
	}

	static uint64_t get_cur_ms() {
		auto now = std::chrono::system_clock::now();
		auto now_time_t = std::chrono::system_clock::to_time_t(now);
		
		// 获取毫秒
		auto duration = now.time_since_epoch();
		auto total_millis = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
		auto millis = total_millis % 1000;
		
		// 转换为本地时间
		std::tm local_time;
		localtime_r(&now_time_t, &local_time);
		
		// 直接计算数值，避免字符串转换
		uint64_t timestamp = 0;
		timestamp += (local_time.tm_year + 1900) * 10000000000000LL;  // 年
		timestamp += (local_time.tm_mon + 1) * 100000000000LL;        // 月
		timestamp += local_time.tm_mday * 1000000000LL;               // 日
		timestamp += local_time.tm_hour * 10000000LL;                 // 时
		timestamp += local_time.tm_min * 100000LL;                    // 分
		timestamp += local_time.tm_sec * 1000LL;                      // 秒
		timestamp += millis;                                          // 毫秒
		
		return timestamp;
	}

	static uint64_t get_current_ms_epoch()
	{
		struct timeval tv;
		gettimeofday(&tv, NULL);
		return tv.tv_sec * 1000 + tv.tv_usec / 1000;
	}

	static uint64_t get_current_ns_epoch()
	{

		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
		// struct timeval tv;
		// gettimeofday(&tv, NULL);
		// return tv.tv_sec * 1000 + tv.tv_usec / 1000;
	}
	static std::string get_current_ns_epoch_string() {
		auto now = std::chrono::system_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm = *std::gmtime(&t); // 获取UTC时间（而非本地时间）
		char buffer[64];
		// 格式：年-月-日T时:分:秒.毫秒Z（Z表示UTC时区）
		snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ", 
				tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, 
				tm.tm_hour, tm.tm_min, tm.tm_sec, ms.count() % 1000);
		return buffer;
	}
	static void cell_sleep_us(int us)
	{
		if (us >= 100)
			std::this_thread::sleep_for(std::chrono::microseconds(us));
		else
			std::this_thread::sleep_for(std::chrono::microseconds(100));
	}


	static std::string getFormattedLocalTime() {
		auto now = std::chrono::system_clock::now();
		std::time_t currentTime = std::chrono::system_clock::to_time_t(now);
		std::tm* localTime = std::localtime(&currentTime);
		if (localTime == nullptr) {
			throw std::runtime_error("无法获取本地时间");
		}

		std::stringstream ss;
		
		// 格式化输出：年(4位)月(2位)日(2位)_时(2位)分(2位)
		ss << std::setfill('0') 
		<< std::setw(4) << (localTime->tm_year + 1900)  // 年份需要加上1900
		<< std::setw(2) << (localTime->tm_mon + 1)      // 月份从0开始，需要加1
		<< std::setw(2) << localTime->tm_mday           // 日期
		<< "_"
		<< std::setw(2) << localTime->tm_hour           // 小时
		<< std::setw(2) << localTime->tm_min;           // 分钟
		
		return ss.str();
	}

	// 以下 5 块用作计算两个yyyymmddhhmmssmmm时间戳间隔，算术方法，高性能
	struct TimeComponents {
		int y, m, d, H, M, S;
	};
	constexpr int DAYS_PER_MONTH[2][12] = {
		{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
		{31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
	};

	constexpr bool isLeapYear(int year) {
		return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
	}

	constexpr uint64_t dateToSeconds(int y, int m, int d, int H, int M, int S) {
		uint64_t total_days = d - 1;
		for (int year = 1970; year < y; ++year) {
			total_days += isLeapYear(year) ? 366 : 365;
		}
		for (int month = 1; month < m; ++month) {
			total_days += DAYS_PER_MONTH[isLeapYear(y)][month - 1];
		}
		return total_days * 86400ULL + H * 3600ULL + M * 60ULL + S;
	}

	inline uint64_t cal_offset_sec(uint64_t ts1, uint64_t ts2) {
		auto extract = [](uint64_t ts) -> TimeComponents {
			ts /= 1000;
			return {
				static_cast<int>(ts / 10000000000ULL % 10000),
				static_cast<int>(ts / 100000000ULL % 100),
				static_cast<int>(ts / 1000000ULL % 100),
				static_cast<int>(ts / 10000ULL % 100),
				static_cast<int>(ts / 100ULL % 100),
				static_cast<int>(ts % 100)
			};
		};

		auto t1 = extract(ts1);
		auto t2 = extract(ts2);
		uint64_t sec1 = dateToSeconds(t1.y, t1.m, t1.d, t1.H, t1.M, t1.S);
		uint64_t sec2 = dateToSeconds(t2.y, t2.m, t2.d, t2.H, t2.M, t2.S);
		return sec2 - sec1;
	}

	// 判断目录是否存在
	static bool directoryExists(const std::string& path) {
		struct stat info;
		if (stat(path.c_str(), &info) != 0) {
			return false;  // 无法获取信息，目录不存在
		}
		return (info.st_mode & S_IFDIR) != 0;  // 判断是否为目录
	}

	// 创建目录（跨平台）
	static bool createDirectory(const std::string& path) {
	#ifdef _WIN32
		// Windows系统使用_mkdir
		int result = _mkdir(path.c_str());
	#else
		// Unix/Linux系统使用mkdir，权限设置为rwxr-xr-x
		int result = mkdir(path.c_str(), 0755);
	#endif
		return result == 0;
	}

	// 如果目录不存在则创建
	static bool makeDirIfNotExist(const std::string& path) {
		if (directoryExists(path)) {
			return true;  // 目录已存在
		}
		if (createDirectory(path)) {
			return true;
		} else {
			return false;
		}
	}


	static std::string str_round(double src, uint8_t step)
	{
		int64_t dv = pow(10, step);
		int64_t inter = (int64_t)src;
		int64_t res = abs((src - inter) * dv);
		std::string res_string;
		if (res != 0)
			res_string = std::to_string(inter) + "." + std::to_string(res);
		else
			res_string = std::to_string(inter);
		if (src < 0.0f)
			res_string = "-" + res_string;
		return res_string;
	}

	static std::vector<std::string> tb_splite(std::string strr, char ch)
	{
		std::vector<std::string> vec;
		std::vector<int> index_vec;

		int len = strr.length();
		for (int i = 0; i < len; ++i)
		{
			if (strr[i] == ch)
				index_vec.push_back(i);
		}

		int vec_len = index_vec.size();
		if (vec_len == 0)
			vec.push_back(strr);
		else
		{
			index_vec.push_back(len);
			for (int i = 0; i <= vec_len; ++i)
			{
				int b = 0, e = index_vec[i] - 1;
				if (i > 0)
				{
					b = index_vec[i - 1] + 1;
				}
				if (e >= b)
					vec.push_back(strr.substr(b, e - b + 1));
			}
		}
		return vec;
	}
};