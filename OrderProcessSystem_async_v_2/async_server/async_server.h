#ifndef SERVER_H
#define SERVER_H
#include <algorithm>
#include <string>
#include <iostream>
#include <unordered_map>
#include <set>
#include <time.h>
#include <mutex>
#include <memory>
#include <thread>
#include <unistd.h>
#include <functional>
#include <stdexcept>
#include <boost/utility.hpp>
#include <boost/type_traits.hpp>
#include <cmath>
#include "../helper/helper.h"
#include "../market/market.h"

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include "../proto/OrderProcessSystem.grpc.pb.h"
#include "assert.h"

using grpc::Server;
using grpc::ServerAsyncReaderWriter;
using grpc::ServerAsyncWriter;
using grpc::ServerAsyncReader;
using grpc::ServerAsyncResponseWriter;
using grpc::ServerCompletionQueue;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

using OPS::NewOrderRequest;
using OPS::CancelOrderRequest;
using OPS::QueryOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderReport;
using OPS::OrderService;

// 基类
class CommonCallData{
public:
	OrderService::AsyncService* service_;
	ServerCompletionQueue* cq_;
	ServerContext ctx_;
	NewOrderRequest newOrderRequest_;
	CancelOrderRequest cancelOrderRequest_;
	QueryOrderRequest queryOrderRequest_;
	ExecutionReport report_;
	NewOrderRequest orderReport_;
	// 交易市场
	TradingMarket* tradingMarket_;
	// 状态机
	enum CallStatus {CREATE, PROCESS, FINISH};
	// 当前的服务状态
	CallStatus status_;
	// 构造函数
	explicit CommonCallData(OrderService::AsyncService*, ServerCompletionQueue*, TradingMarket*);
	// 析构函数
	virtual ~CommonCallData(){}
	virtual void Proceed(bool=true)=0;
};

// 处理新订单类
class CallDataPushNewOrder:public CommonCallData{
private:
	ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest> responder_;
	bool new_responder_created_;
	bool writing_mode_;
	uint32_t RequestsCounter_;
	uint32_t ReportsCounter_;
	std::vector<std::pair<uint64_t, ExecutionReport> > reports_;
	static std::unordered_map<uint64_t, ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest>*> orderID_responder_;
public:
	CallDataPushNewOrder(OrderService::AsyncService*, ServerCompletionQueue*, TradingMarket*);
	virtual void Proceed(bool =true) override;
};
std::unordered_map<uint64_t, ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest>*> CallDataPushNewOrder::orderID_responder_;

// 处理撤销订单
class CallDataPushCancelOrder:public CommonCallData{
private:
	ServerAsyncResponseWriter<ExecutionReport> responder_;
	
public:
	CallDataPushCancelOrder(OrderService::AsyncService*, ServerCompletionQueue*, TradingMarket*);
	virtual void Proceed(bool = true) override;
};

// 处理查询订单
class CallDataPushQueryOrder:public CommonCallData{
private:
	ServerAsyncWriter<OrderReport> responder_;
	bool new_responder_created_;
	uint32_t reportsCounter_;
	std::vector<OrderReport> queryOrderReports_;
public:
	CallDataPushQueryOrder(OrderService::AsyncService*, ServerCompletionQueue*, TradingMarket*);
	virtual void Proceed(bool =true) override;
};

// 服务端类
class ServerImpl final{
public:
	ServerImpl(){
		tradingMarket_=TradingMarket::getInstance();
	}
	~ServerImpl(){
		server_->Shutdown();
		cq_->Shutdown();	
		delete tradingMarket_;
	}
	void Run();
private:
	std::unique_ptr<ServerCompletionQueue> cq_;
 	OrderService::AsyncService service_;
  	std::unique_ptr<Server> server_;
	TradingMarket* tradingMarket_;
	void HandleRpcs();
};
#endif