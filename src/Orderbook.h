#pragma once
#include "Globals.h"
#include <map>
#include <unordered_map>
#include <list>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

class Order {
public:
    Order(OrderType type, OrderId id, Side side, Price price, Quantity qty, bool isMM = false);
    OrderType GetOrderType() const;
    OrderId GetOrderId() const;
    Side GetSide() const;
    Price GetPrice() const;
    Quantity GetInitialQuantity() const;
    Quantity GetRemainingQuantity() const;
    bool IsFilled() const;
    void Fill(Quantity q);
    void ToGoodTillCancel(Price p);
    bool isMM_;
private:
    OrderType type_;
    OrderId id_;
    Side side_;
    Price price_;
    Quantity initialQty_, remQty_;
};

using OrderPtr = std::shared_ptr<Order>;

struct LevelData {
    enum class Action { Add, Remove, Match };
    Quantity quantity_ = 0;
    uint32_t count_ = 0;
};

class Orderbook {
public:
    Orderbook();
    ~Orderbook();
    void Execute(OrderPtr order);
    void Cancel(OrderId id);
    
    struct Snap { std::vector<std::pair<Price, Quantity>> bids, asks; };
    Snap GetSnap();

private:
    void AddInternal(OrderPtr o);
    void CancelInternal(OrderId id);
    void MatchOrders();
    void UpdateLevelData(Price p, Quantity q, LevelData::Action a);
    void PruneGoodForDayOrders();

    std::map<Price, std::list<OrderPtr>> bids_, asks_;
    std::unordered_map<OrderId, std::pair<OrderPtr, std::list<OrderPtr>::iterator>> orders_;
    std::map<Price, LevelData> data_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::thread pruneThread_;
};