#include "Globals.h"
#include "Orderbook.h"
#include <iostream>
#include <sstream>
#include <random>
#include <set>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

// DEFINIREA VARIABILELOR GLOBALE AICI
std::atomic<Price>   g_mid{100};
std::atomic<bool>    g_running{true};
std::atomic<OrderId> g_id{1};
std::atomic<int>     g_mmInv{0};
std::atomic<Price>   g_mmRes{100};
std::atomic<double>  g_sigma{1.5};
std::mutex g_tradeMtx;
std::deque<TradeLog> g_recentTrades;

//SSE SERVER LOGIC
struct ClientRegistry {
    std::mutex m;
    std::set<int> fds;
    void Add(int fd) { std::lock_guard<std::mutex> l(m); fds.insert(fd); }
    void Remove(int fd) { std::lock_guard<std::mutex> l(m); fds.erase(fd); close(fd); }
    void Broadcast(const std::string& data) {
        std::string msg = "data: " + data + "\n\n";
        std::vector<int> dead;
        {
            std::lock_guard<std::mutex> l(m);
            for (int fd : fds) {
                #ifdef __APPLE__
                if (send(fd, msg.c_str(), msg.size(), 0) <= 0) dead.push_back(fd);
                #else
                if (send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL) <= 0) dead.push_back(fd);
                #endif
            }
        }
        for (int fd : dead) Remove(fd);
    }
} g_clients;

void Broadcaster(Orderbook& book) {
    while (g_running) {
        auto snap = book.GetSnap();
        std::ostringstream oss;
        oss << "{\"mid\":" << g_mid.load() << ",\"inv\":" << g_mmInv.load() 
            << ",\"res\":" << g_mmRes.load() << ",\"sig\":" << g_sigma.load() << ",\"bids\":[";
        for (size_t i = 0; i < snap.bids.size() && i < 10; ++i) {
            if (i) oss << ","; oss << "{\"p\":" << snap.bids[i].first << ",\"q\":" << snap.bids[i].second << "}";
        }
        oss << "],\"asks\":[";
        for (size_t i = 0; i < snap.asks.size() && i < 10; ++i) {
            if (i) oss << ","; oss << "{\"p\":" << snap.asks[i].first << ",\"q\":" << snap.asks[i].second << "}";
        }
        oss << "],\"trades\":[";
        {
            std::lock_guard<std::mutex> tl(g_tradeMtx);
            for (size_t i = 0; i < g_recentTrades.size(); ++i) {
                if (i) oss << ",";
                oss << "{\"s\":\"" << g_recentTrades[i].side << "\",\"p\":" << g_recentTrades[i].price << ",\"q\":" << g_recentTrades[i].qty << "}";
            }
        }
        oss << "]}";
        g_clients.Broadcast(oss.str());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// TRADING STRATEGIES
void AvellanedaStoikovMM(Orderbook& book) {
    OrderId lb = 0, la = 0;
    double gamma = 0.01; 
    const int MAX_INV = 1000;

    while (g_running) {
        int q = g_mmInv.load();
        Price s = g_mid.load();
        double sig = g_sigma.load();

        double resPriceRaw = (double)s - ((double)q * gamma * sig);
        Price r = (Price)std::max(10.0, std::min(1000.0, resPriceRaw));
        g_mmRes.store(r);

        if (lb) { book.Cancel(lb); lb = 0; }
        if (la) { book.Cancel(la); la = 0; }

        if (q < MAX_INV) {
            lb = g_id++;
            book.Execute(std::make_shared<Order>(OrderType::GoodTillCancel, lb, Side::Buy, r - 1, (q > 300) ? 5 : ((q < -300) ? 100 : 50), true));
        }
        if (q > -MAX_INV) {
            la = g_id++;
            book.Execute(std::make_shared<Order>(OrderType::GoodTillCancel, la, Side::Sell, r + 1, (q < -300) ? 5 : ((q > 300) ? 100 : 50), true));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
}

void TraderAgent(Orderbook& book) {
    std::mt19937 gen(std::random_device{}());
    while (g_running) {
        std::normal_distribution<double> pd(100.0, 3.0); 
        Price p = (Price)pd(gen);
        Side side = (gen() % 2 == 0) ? Side::Buy : Side::Sell;
        Quantity q = (gen() % 15) + 1;

        if (p > 0 && p < 10000) {
            book.Execute(std::make_shared<Order>(OrderType::GoodTillCancel, g_id++, side, p, q, false));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

// MAIN
int main() {
    Orderbook book;
    std::vector<std::thread> workers;
    workers.emplace_back(AvellanedaStoikovMM, std::ref(book));
    workers.emplace_back(TraderAgent, std::ref(book));
    workers.emplace_back(TraderAgent, std::ref(book)); 
    workers.emplace_back(Broadcaster, std::ref(book));

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    #ifdef __APPLE__
    setsockopt(srv, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
    #endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(9001);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(srv, (sockaddr*)&addr, sizeof(addr));
    listen(srv, 10);

    std::cout << "OmniMatch Engine Live pe http://localhost:9001\n";

    while (g_running) {
        int cli = accept(srv, NULL, NULL);
        if (cli >= 0) {
            #ifdef __APPLE__
            setsockopt(cli, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof(opt));
            #endif
            std::string h = "HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\n\r\n";
            send(cli, h.c_str(), h.size(), 0);
            g_clients.Add(cli);
        }
    }
    return 0;
}