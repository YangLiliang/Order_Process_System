#include <algorithm>
#include <string>
#include <iostream>
#include <unordered_map>
#include <set>
#include <time.h>
#include <mutex>
#include <utility>
#include "market.h"
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

// 初始化m_instance为NULL
TradingMarket *TradingMarket::m_instance=NULL;

// 服务端类：订单服务
class OrderServiceImpl final : public OrderService::Service {
public:
	// 构造函数
	explicit OrderServiceImpl(){
		tradingMarket=TradingMarket::getInstance();
	}

	// 析构函数
	~OrderServiceImpl(){
	}

	// 报单
	Status PushNewOrder(ServerContext* context, ServerReaderWriter<ExecutionReport, NewOrderRequest>* stream) override{
		NewOrderRequest request;
		// 流：读入请求
		while(stream->Read(&request)){
			// 处理订单请求
			tradingMarket->processNewOrder(request, stream);
		}
		return Status::OK;
	}
	// 撤单
	Status PushCancelOrder(ServerContext* context, const CancelOrderRequest* reader, ExecutionReport* writer) override{
		// 输入撤单请求
		CancelOrderRequest request=*reader;	
		// 初始化应答消息
		ExecutionReport report;
		initReport(report, request);
		// 处理撤销请求
		tradingMarket->processCancelOrder(request, report);
		// 根据撤销订单请求做出应答消息	
		*writer=report;	
		return Status::OK;
	}

private:
	TradingMarket* tradingMarket;
}; 


// 运行服务端
void RunServer(){
	std::string server_address("0.0.0.0:50001");
	OrderServiceImpl service;
	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout<<"Server listening on: "<<server_address<<std::endl;
	server->Wait();
	//server->Shutdown();
	//std::cout<<"Server Shutdown"<<std::endl;
}

int main(int argc, char** argv){
	RunServer();
	return 0;
}
