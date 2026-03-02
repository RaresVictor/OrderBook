#include "Orderbook.h"
#include <algorithm>

// Order Class Implementation
Order::Order(OrderType type, OrderId id, Side side, Price price, Quantity qty, bool isMM)
    :isMM_(isMM), type_(type), id_(id), side_(side), price_(price), initialQty_(qty), remQty_(qty){}

OrderType Order::GetOrderType() const { return type_; }
OrderId Order::GetOrderId() const { return id_; }
Side Order::GetSide() const { return side_; }
Price Order::GetPrice() const { return price_; }
Quantity Order::GetInitialQuantity() const { return initialQty_; }
Quantity Order::GetRemainingQuantity() const { return remQty_; }
bool Order::IsFilled() const { return remQty_ == 0; }
void Order::Fill(Quantity q) { remQty_ -= q; }
void Order::ToGoodTillCancel(Price p) { type_ = OrderType::GoodTillCancel; price_ = p; }

Orderbook::Orderbook() { pruneThread_ = std::thread([this] { PruneGoodForDayOrders(); }); }
Orderbook::~Orderbook() { g_running = false; cv_.notify_all(); if (pruneThread_.joinable()) pruneThread_.join(); }

void Orderbook::Execute(OrderPtr order) {
    std::scoped_lock lock(mtx_);
    if (orders_.contains(order->GetOrderId())) return;

    if (order->GetOrderType() == OrderType::Market) {
        if (order->GetSide() == Side::Buy && !asks_.empty()) order->ToGoodTillCancel(asks_.begin()->first);
        else if (order->GetSide() == Side::Sell && !bids_.empty()) order->ToGoodTillCancel(bids_.rbegin()->first);
        else return;
    }
    AddInternal(order);
    MatchOrders();
}

void Orderbook::Cancel(OrderId id) { std::scoped_lock lock(mtx_); CancelInternal(id); }

Orderbook::Snap Orderbook::GetSnap() {
    std::scoped_lock lock(mtx_);
    Snap s;
    for (auto it = bids_.rbegin(); it != bids_.rend(); ++it) {
        Quantity q = 0; for (auto& o : it->second) q += o->GetRemainingQuantity();
        s.bids.push_back({it->first, q});
    }
    for (auto& [p, l] : asks_) {
        Quantity q = 0; for (auto& o : l) q += o->GetRemainingQuantity();
        s.asks.push_back({p, q});
    }
    return s;
}

void Orderbook::AddInternal(OrderPtr o) {
    auto& stack = (o->GetSide() == Side::Buy) ? bids_ : asks_;
    auto& list = stack[o->GetPrice()];
    list.push_back(o);
    orders_[o->GetOrderId()] = {o, std::prev(list.end())};
    UpdateLevelData(o->GetPrice(), o->GetInitialQuantity(), LevelData::Action::Add);
}

void Orderbook::CancelInternal(OrderId id) {
    if (!orders_.contains(id)) return;
    auto [order, it] = orders_[id];
    auto& stack = (order->GetSide() == Side::Buy) ? bids_ : asks_;
    UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
    stack[order->GetPrice()].erase(it);
    if (stack[order->GetPrice()].empty()) stack.erase(order->GetPrice());
    orders_.erase(id);
}

void Orderbook::MatchOrders() {
    while (!bids_.empty() && !asks_.empty()) {
        auto& [bidPrice, bids] = *bids_.rbegin();
        auto& [askPrice, asks] = *asks_.begin();
        if (bidPrice < askPrice) break;

        while (!bids.empty() && !asks.empty()) {
            auto b = bids.front(); auto a = asks.front();
            Quantity q = std::min(b->GetRemainingQuantity(), a->GetRemainingQuantity());
            
            b->Fill(q); a->Fill(q);
            if (askPrice > 10 && askPrice < 10000) g_mid.store(askPrice);
            if (b->isMM_) g_mmInv += q;
            if (a->isMM_) g_mmInv -= (int)q;

            {
                std::lock_guard<std::mutex> tl(g_tradeMtx);
                g_recentTrades.push_front({ b->GetOrderId() > a->GetOrderId() ? "buy" : "sell", askPrice, q });
                if (g_recentTrades.size() > 25) g_recentTrades.pop_back();
            }

            UpdateLevelData(b->GetPrice(), q, b->IsFilled() ? LevelData::Action::Remove : LevelData::Action::Match);
            UpdateLevelData(a->GetPrice(), q, a->IsFilled() ? LevelData::Action::Remove : LevelData::Action::Match);

            if (b->IsFilled()) { orders_.erase(b->GetOrderId()); bids.pop_front(); }
            if (a->IsFilled()) { orders_.erase(a->GetOrderId()); asks.pop_front(); }
        }
        if (bids.empty()) bids_.erase(bidPrice);
        if (asks.empty()) asks_.erase(askPrice);
    }
}

void Orderbook::UpdateLevelData(Price p, Quantity q, LevelData::Action a) {
    auto& d = data_[p];
    d.count_ += (a == LevelData::Action::Remove ? -1 : (a == LevelData::Action::Add ? 1 : 0));
    if (a == LevelData::Action::Remove || a == LevelData::Action::Match) d.quantity_ -= q;
    else d.quantity_ += q;
    if (d.count_ == 0) data_.erase(p);
}

void Orderbook::PruneGoodForDayOrders() {
    while (g_running) {
        std::unique_lock lock(mtx_);
        if (cv_.wait_for(lock, std::chrono::seconds(10), []{ return !g_running; })) return;
        std::vector<OrderId> toCancel;
        for (auto& [id, entry] : orders_) { if (entry.first->GetOrderType() == OrderType::GoodForDay) toCancel.push_back(id); }
        for (auto id : toCancel) CancelInternal(id);
    }
}