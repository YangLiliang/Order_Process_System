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
#include <shared_mutex>
#include <thread>
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
using OPS::QueryOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderReport;
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
	// 根据查询订单请求做出应答消息
	void processQueryOrder(const QueryOrderRequest&, std::vector<OrderReport>&);
private:
	// 构造函数
	TradingMarket(){
		id=0;
		market=5.0;
	}
	// 实例
	static TradingMarket* m_instance;

	// 存放订单的容器<orderID, order>, 插入与删除需要互斥
	std::unordered_map<uint64_t, NewOrderRequest> orders; 
	// <orderID, order_mutex>, 插入与删除需要互斥
	//std::unordered_map<uint64_t, std::mutex*> order_mutex;
	// 插入和删除订单容器的互斥量
	std::shared_mutex rw_orders_mutex;

	// 需要被售卖或购买的容器，<stockID, sell and buy container>, 插入与删除需要互斥
	std::unordered_map<std::string, SellAndBuyContainer> sell_buy_containers;
	// <stockID, pair<first: sell_mutex, second: buy_mutex> >, 插入与删除需要互斥
	std::unordered_map<std::string, std::pair<std::mutex*, std::mutex*> > stock_mutex; 
	// 插入和删除股票容器的互斥量
	std::shared_mutex rw_stocks_mutex;

	// 删除操作之间互斥
	std::shared_mutex cancel_mutex;

	// 订单ID，动态增加，增加需要互斥
	uint64_t id;
	// 订单ID增加的互斥锁
	std::mutex orderID_mutex;

	// 市场价
	double market; 

	// 创建订单
	uint64_t createOrder(const NewOrderRequest&);
	// 插入新订单
	void insertOrder(const uint64_t&, const NewOrderRequest&);
	// 修改订单
	void alterOrder(const uint64_t&, const NewOrderRequest&);
	// 删除订单(删除成功返回true)
	bool deleteOrder(const uint64_t&);
	// 卖订单
	void sellOrders(const uint64_t&, const std::string&, std::vector<std::pair<uint64_t, ExecutionReport> >&);
	// 买订单
	void buyOrders(const uint64_t&, const std::string&, std::vector<std::pair<uint64_t, ExecutionReport> >&);
	// 查询订单
	NewOrderRequest selectOrder(const uint64_t&);
	// 判断订单存在
	bool existInOrders(const uint64_t&);
	// 判断订单存在且获取订单
	bool isExistAndGetOrder(const uint64_t&, NewOrderRequest&);
	// 获取所有订单
	void getAllOrders(std::vector<OrderReport>&);

	// 插入新股票
	void insertStock(const std::string&);
	// 将订单加入至待售卖容器
	void addOrderToSell(const std::string&, const uint64_t&);
	// 将订单加入至待购买容器
	void addOrderToBuy(const std::string&, const uint64_t&);
	// 将订单从售卖容器中删除
	void delOrderFromSell(const std::string&, const uint64_t&);
	// 将订单从购买容器中删除
	void delOrderFromBuy(const std::string&, const uint64_t&);
	// 判断该股票订单是否在容器中
	bool existInContainers(const std::string&);
	// 获取订单集合的引用
	std::set<uint64_t>& getSellOrderSet(const std::string&);
	// 获取买订单集合的引用
	std::set<uint64_t>& getBuyOrderSet(const std::string&);
};
// TradingMarket* TradingMarket::m_instance=new TradingMarket;
#endif
