enum class Exchange : uint32_t
{
    OKX,
    BINANCE_SPOT,
    BINANCE_U_MARGIN,
    BINANCE_COIN_MARGIN,
    BYBIT_SPOT,
    BYBIT_U_MARGIN,
    BYBIT_COIN_MARGIN,
    BITGET_SPOT = 8,
    BITGET_MARGIN,
    BITGET_USDT_FUTURES,
    BITGET_USDC_FUTURES,
    BITGET_COIN_FUTURES,
    HYPERLIQUID,
    DERIBIT,
    GATEIO_SPOT,
    GATEIO_U_MARGIN,
    GATEIO_BTC_MARGIN,
    COINBASE,
    SIMULATION,
    UNKNOWN = 128
};

enum class ProductType : uint8_t
{
    SPOT,
    FUTURES,
    UMARGIN,
    COINMARGIN,
    OPTIONS,
    USDT_FUTURES,
    USDC_FUTURES,
    BTCMARGIN,
    UNKNOWN = 128
};

enum class MessageType : int8_t
{
    DATA_ERROR = -1,
    MARKETDATA_BOO = 0,
    MARKETDATA_TRADE,
    MARKETDATA_AGGTRADE,
    MARKETDATA_DEPTH5,
    USERDATA_ORDER_UPDATE,
    USERDATA_TRADE_UPDATE,
    USERDATA_QUERY_ORDER_RESPONSE,
    USERDATA_QUERY_INTERNAL_RESPONSE,

    DATA_UNKNOWN = 100,
    CGW_HEARTBEAT,
    UNKNOWN = 127
};

enum class TradeType : uint8_t
{
    MARKET,
    ADL,
    INSURANCE_FUND,
    UNKNOWN = 128
};

enum class RequestType : uint8_t
{
    ORDER,
    CANCEL,
    QUERY_ORDER,
    INTERNAL_QUERY,
    UNKNOWN = 128
};

enum class ErrorType : int32_t
{
    DISCONNECTION = 1,
    RECONNECTION,
    ALIVE,
    UNKNOWN = 128
};

enum class OrderStatus : int8_t
{
    INTERNAL_ORDER_FAILURE = -4,
    INTERNAL_CANCEL_FAILURE = -3,
    ORDER_FAILURE = -2,
    CANCEL_FAILURE = -1,
    PENDING_ORDER = 0,
    NO_CANCEL,
    ACK,
    LIVED,
    PARTIAL_TRADED,
    ALL_TRADED,
    PENDING_CANCEL,
    CANCELLED,
    MMP_CANCELLED,
    UNKNOWN = -128
};

enum class TradingMode : uint8_t
{
    CASH = 0,
    CROSS,
    ISOLATED,
    UNKNOWN = 128
};

enum class OrderType : uint8_t
{
    POST_ONLY = 0,
    MARKET,
    LIMIT,
    GTC,
    FOK,
    IOC,
    OPTIMAL_LIMIT_IOC,
    MMP,
    MMP_AND_POST_ONLY,
    RPI,
    UNKNOWN = 128
};

enum class InternalRequestType : uint8_t
{
    EXCHANGE_CONNECTION_STATUS = 1,
    UNKNOWN = 128
};

enum class InternalResponseConnectionStatusType : uint8_t
{
    CONNECTED = 1,
    DISCONNECTED = 2,
    UNKNOWN = 128
};

enum class InternalOrderResponseErrorType : uint8_t
{
    EXCHANGE_DISCONNECTED = 1,
    WRONG_CHECKSUM = 2,
    UNKNOWN = 128
};

enum class AccountType : uint8_t
{
    SPOT,
    USDT_SPOT,
    USDC_SPOT,
    USD_SPOT,
    CONTRACT,
    USDT_CONTRACT,
    USDC_CONTRACT,
    COIN_CONTRACT,
    FIAT,
    INVERSE,
    LINEAR,

    UNKNOWN = 128
};

enum class ShortExchange : uint8_t
{
    BIT,
    BINANCE,
    OKX,
    DYDX,
    GATE,
    DERIBIT,
    UPBIT,
    BITFINEX,
    BYBIT,
    BITGET,
    HYPERLIQUID,
    UNKNOWN = 128
};

enum class SymbolType : uint8_t
{
    SWAP,
    SPOT,
    FUTURE,
    FIAT,
    UNKNOWN = 128
};

enum class Asset : uint8_t
{
    BTC,
    ETH,
    SOL,
    USD,
    USDT,
    USDC,
    EUR,
    KRW,
    UNKNOWN = 128
};

enum class StrategyCommandType : uint8_t
{
    PAUSE_TRADING,
    RESUME_TRADING,
    REFRESH_FEES,
    ACTIVATE_ACCOUNT,
    DEACTIVATE_ACCOUNT,
    ENABLE_REDUCE_ONLY,
    DISABLE_REDUCE_ONLY,
    ENABLE_BUY_ACCOUNT,
    ENABLE_SELL_ACCOUNT,
    DISBALE_BUY_ACCOUNT,
    DISABLE_SELL_ACCOUNT,
    LOAD_INITIAL_RISK,
    UNKNOWN = 128,
};

enum class TradeStreamType : uint8_t
{
    RAW,
    AGGREGATE,
    PRIVATE,
    UNKNOWN = 128
};


struct Depth5
{
    char m_symbol[32];
    uint64_t m_tradeTimestamp;
    uint64_t m_eventTimestamp;
    uint64_t m_updateId;
    uint32_t m_bidLevel;
    uint32_t m_askLevel;
    double m_lastPrice;
    std::array<double, 5> m_bidPrice;
    std::array<double, 5> m_bidVol;
    std::array<double, 5> m_askPrice;
    std::array<double, 5> m_askVol;
    std::array<int, 5> m_bidOrderCount;
    std::array<int, 5> m_askOrderCount;
    double m_reserved;

};

struct Bookticker
{
    char m_symbol[32];
    ProductType m_type;
    // ShortExchange exchange;
    // Exchange exchange
    uint64_t m_eventTimestamp;
    uint64_t m_tradeTimestamp;
    uint64_t m_updateId;
    double m_bidPrice;
    double m_bidVol;
    double m_askPrice;
    double m_askVol;
    int m_bidOrderCount;
    int m_askOrderCount;
    uint64_t m_LocalTime_us;

    Bookticker();
};

struct Trade
{
    char m_symbol[32];
    uint64_t m_eventTimestamp;
    uint64_t m_tradeTimestamp;
    uint64_t m_tradeId;
    double m_price;
    double m_vol;
    TradeType m_type;
    bool m_isBuy;
};

struct AggTrade
{
    char m_symbol[32];
    ProductType m_type;
    uint64_t m_eventTimestamp;
    uint64_t m_tradeTimestamp;
    uint64_t m_firstTradeId;
    uint64_t m_lastTradeId;
    uint64_t m_aggTradeId;
    double m_price;
    double m_vol;
    int m_tradeCount;
    bool m_isBuy;
    uint64_t m_LocalTime_us;
};