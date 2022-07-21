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
using OPS::ExecutionReport;
using OPS::OrderService;

// 基类
class CommonCallData{
public:
	OrderService::AsyncService* service_;
	ServerCompletionQueue* cq_;
	ServerContext ctx_;
	NewOrderRequest newOrderRequest_;
	CancelOrderRequest cancelOrderRequest_;
	ExecutionReport report_;
	// 交易市场
	TradingMarket* tradingMarket_;
	// 状态机
	enum CallStatus {CREATE, PROCESS, FINISH};
	// 当前的服务状态
	CallStatus status_;
	// 构造函数
	explicit CommonCallData(OrderService::AsyncService* service, ServerCompletionQueue* cq, TradingMarket* tradingMarket):
			service_(service), cq_(cq), status_(CREATE), tradingMarket_(tradingMarket){}
	// 析构函数
	virtual ~CommonCallData(){}
	virtual void Proceed(bool=true)=0;
};

// 处理新订单类
class CallDataPushNewOrder:public CommonCallData{
private:
	ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest> responder_;
	bool new_responder_created;
	bool writing_mode_;
	uint32_t RequestsCounter;
	uint32_t ReportsCounter;
	std::vector<std::pair<uint64_t, ExecutionReport> > reports;
	static std::unordered_map<uint64_t, ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest>*> orderID_responder;
public:
	CallDataPushNewOrder(OrderService::AsyncService* service, ServerCompletionQueue* cq, TradingMarket* tradingMarket);
	virtual void Proceed(bool ok=true) override;
};
std::unordered_map<uint64_t, ServerAsyncReaderWriter<ExecutionReport, NewOrderRequest>*> CallDataPushNewOrder::orderID_responder;

// 处理撤销订单
class CallDataPushCancelOrder:public CommonCallData{
private:
	ServerAsyncResponseWriter<ExecutionReport> responder_;
	CancelOrderRequest request_;
	ExecutionReport report_;
	
public:
	CallDataPushCancelOrder(OrderService::AsyncService* service, ServerCompletionQueue* cq, TradingMarket* tradingMarket);
	virtual void Proceed(bool ok= true) override;
};

// 服务端类
class ServerImpl final{
public:
	ServerImpl(){
		tradingMarket=TradingMarket::getInstance();
	}
	~ServerImpl(){
		server_->Shutdown();
		cq_->Shutdown();	
		delete tradingMarket;
	}
	void Run();
private:
	std::unique_ptr<ServerCompletionQueue> cq_;
 	OrderService::AsyncService service_;
  	std::unique_ptr<Server> server_;
	TradingMarket* tradingMarket;
};
#endif