#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <deque>
#include <mutex>

enum class OrderType { 
    GoodTillCancel, 
    FillAndKill, 
    FillOrKill, 
    Market, 
    GoodForDay 
};
enum class Side { 
    Buy, 
    Sell 
};

using Price = std::int32_t;
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;

struct TradeLog {
    std::string side;
    Price price;
    Quantity qty;
};

// Declarații externe (le vom defini real în main.cpp)
extern std::atomic<Price>   g_mid;
extern std::atomic<bool>    g_running;
extern std::atomic<OrderId> g_id;
extern std::atomic<int>     g_mmInv;
extern std::atomic<Price>   g_mmRes;
extern std::atomic<double>  g_sigma;

extern std::mutex g_tradeMtx;
extern std::deque<TradeLog> g_recentTrades;