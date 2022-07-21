#ifndef MARKET_H
#define MARKET_H

#include <algorithm>
#include <string>
#include <iostream>
#include <unordered_map>
#include <set>
#include <time.h>
#include <mutex>
#include <utility>
#include "../helper/helper.h"

#include <grpc/grpc.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <grpcpp/security/server_credentials.h>
#include "../proto/OrderProcessSystem.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerReader;
using grpc::ServerReaderWriter;
using grpc::ServerWriter;
using grpc::Status;

using OPS::NewOrderRequest;
using OPS::CancelOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderService;

const double MINN=1e-6;

// 售卖容器和购买容器
struct SellAndBuyContainer{
	std::set<uint64_t> sell;
	std::set<uint64_t> buy;
};

// 交易市场：单例模式 饿汉模式 无线程安全问题
class TradingMarket{
public:
	// 获取实例
	static TradingMarket* getInstance(){
		//if(m_instance==NULL) m_instance=new TradingMarket();
		return m_instance;
	}
	// 根据新订单请求做出应答消息
	void processNewOrder(const NewOrderRequest&, std::vector<std::pair<uint64_t, ExecutionReport> >&, uint64_t&);
	// 根据撤销订单请求做出应答消息
	void processCancelOrder(const CancelOrderRequest&, ExecutionReport&);
private:
	// 构造函数
	TradingMarket(){
		id=0;
		market=5.0;
	}
	// 实例
	static TradingMarket* m_instance;
	// 存放订单的容器<orderID, order>
	std::unordered_map<uint64_t, NewOrderRequest> orders; 
	// 需要被售卖或购买的容器，<stockID, sell and buy container>
	std::unordered_map<std::string, SellAndBuyContainer> sell_buy_containers;
	// 订单ID，动态增加
	uint64_t id;
	// 创建订单
	uint64_t createOrder(const NewOrderRequest&);
	// 修改订单
	void alterOrder(const uint64_t&, const NewOrderRequest&);
	// 删除订单
	void deleteOrder(const uint64_t&);
	// 卖订单
	void sellOrders(const uint64_t&, const std::string&, std::vector<std::pair<uint64_t, ExecutionReport> >&);
	// 买订单
	void buyOrders(const uint64_t&, const std::string&, std::vector<std::pair<uint64_t, ExecutionReport> >&);
	// 将订单加入至待售卖容器
	void addOrderToSell(const std::string&, const uint64_t&);
	// 将订单加入至待购买容器
	void addOrderToBuy(const std::string&, const uint64_t&);
	// 市场价
	double market; 
	// 访问互斥锁
	std::mutex orderID_mutex, cancel_mutex;
	// <stockID, pair<first: sell_mutex, second: buy_mutex> >
	std::unordered_map<std::string, std::pair<std::mutex*, std::mutex*> > stock_mutex; 
	// 订单ID对应的stream
	// std::unordered_map<uint64_t, ServerReaderWriter<ExecutionReport, NewOrderRequest>*> orderID_stream;
};
//TradingMarket* TradingMarket::m_instance=new TradingMarket;
#endif
