#ifndef CLIENT_H
#define CLIENT_H

#include <string>
#include <memory>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <functional>
#include <stdexcept>
#include <boost/utility.hpp>
#include <boost/type_traits.hpp>
#include <cmath>
#include <fstream>
#include <iostream>
#include "../helper/helper.h"
#include "assert.h"

#include <grpc++/grpc++.h>
#include <grpc/support/log.h>
#include "../proto/OrderProcessSystem.grpc.pb.h"

#define TYPE_LIMIT true
#define TYPE_MARKET false
#define DIRE_SELL true
#define DIRE_BUY false

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientAsyncReader;
using grpc::ClientAsyncReaderWriter;
using grpc::ClientAsyncWriter;
using grpc::ClientAsyncResponseReader;
using grpc::CompletionQueue;
using grpc::Status;

using OPS::NewOrderRequest;
using OPS::CancelOrderRequest;
using OPS::QueryOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderService;

// 创建新订单请求
NewOrderRequest MakeNewOrderRequest(const bool&, const bool&, 
				const uint64_t&, const std::string&,
				const uint32_t&, const double&);

// 创建撤销订单请求
CancelOrderRequest MakeCancelOrderRequest(const uint64_t&);

// 创建查询订单请求
QueryOrderRequest MakeQueryOrderRequest();

// 读入新订单文件
void readNewOrderRequest(const std::string&, std::vector<NewOrderRequest>&);

// 抽象类
class AbstractAsyncClientCall{
public:
    // 状态机
	enum CallStatus {PROCESS, FINISH, DESTROY};
	explicit AbstractAsyncClientCall():callStatus(PROCESS){}
	virtual ~AbstractAsyncClientCall(){}
    // 上下文
	ClientContext context;
	Status status;
	CallStatus callStatus;
	ExecutionReport report_;
	NewOrderRequest queryReport_;
	virtual void Proceed(bool = true) = 0;
};

// 撤销订单类
class AsyncClientCallPushCancelOrder: public AbstractAsyncClientCall{
private:
	std::unique_ptr<ClientAsyncResponseReader<ExecutionReport> > responder;
public:
	AsyncClientCallPushCancelOrder(const CancelOrderRequest& request, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_);
	virtual void Proceed(bool ok = true) override;
};

// 提交订单类
class AsyncClientCallPushNewOrder:public AbstractAsyncClientCall{
private:
	std::unique_ptr<ClientAsyncReaderWriter<NewOrderRequest, ExecutionReport> >responder_;
	uint32_t counter;
	bool writing_mode_;
	std::vector<NewOrderRequest> requests_;
public:
	AsyncClientCallPushNewOrder(std::vector<NewOrderRequest>&& requests, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_);
	virtual void Proceed(bool ok = true) override;
};

// 查询订单类
class AsyncClientCallPushQueryOrder:public AbstractAsyncClientCall{
private:
	std::unique_ptr< ClientAsyncReader<NewOrderRequest> > responder;
public:
	AsyncClientCallPushQueryOrder(const QueryOrderRequest& request, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_);
	virtual void Proceed(bool ok = true) override;
};


// 客户端类
class OPSClient{
private:
	std::unique_ptr<OrderService::Stub>stub_;
	CompletionQueue cq_;
public:
	explicit OPSClient(std::shared_ptr<Channel> channel);
	// 提交订单
	void PushNewOrder(const std::string& fileName);
	// 撤销订单
	void PushCancelOrder(const uint64_t& orderID);
	// 查询订单
	void PushQueryOrder();
	// 异步处理完成队列中的事件
	void AsyncCompleteRpc();
};
#endif