#include <algorithm>
#include <string>
#include <iostream>
#include <unordered_map>
#include <set>
#include <time.h>
#include <mutex>
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


// 服务端类：订单服务
class OrderServiceImpl final : public OrderService::Service {
public:
	// 构造函数
	explicit OrderServiceImpl(){
		this->id=0;
		this->current=5.0;
		process_mutex=new std::mutex();
	}

	// 析构函数
	~OrderServiceImpl(){
		delete process_mutex;
	}
	// 报单
	Status PushNewOrder(ServerContext* context, ServerReaderWriter<ExecutionReport, NewOrderRequest>* stream) override{
		NewOrderRequest request;
		ExecutionReport report;
		while(stream->Read(&request)){
			// 初始化应答消息
			initReport(report, request);
			// 根据新订单请求做出应答消息
			processNewOrder(request, report);
			stream->Write(report);
		}
		return Status::OK;
	}
	// 撤单
	Status PushCancelOrder(ServerContext* context, const CancelOrderRequest* reader, ExecutionReport* writer) override{
		CancelOrderRequest request=*reader;	
		ExecutionReport report;
		// 初始化应答消息
		initReport(report, request);
		// 根据撤销订单请求做出应答消息
		processCancelOrder(request, report);	
		*writer=report;	
		return Status::OK;
	}

private:
	// 存放订单的容器<orderID, order>
	std::unordered_map<uint64_t, NewOrderRequest> orders; 
	// 需要被售卖的stocks，<stockID, 属于stockID的orderIDs>
	std::unordered_map<std::string, std::set<uint64_t>> sell;
	// 需要被购买的stocks, <stockID, 属于stockID的orderIDs>
	std::unordered_map<std::string, std::set<uint64_t>> buy;
	// 订单ID，动态增加
	uint64_t id;
	// 根据新订单请求做出应答消息
	void processNewOrder(const NewOrderRequest&, ExecutionReport&);
	// 根据撤销订单请求做出应答消息
	void processCancelOrder(const CancelOrderRequest&, ExecutionReport&);
	// 创建订单
	uint64_t createOrder(const NewOrderRequest&);
	// 修改订单
	void alterOrder(const uint64_t&, const NewOrderRequest&);
	// 删除订单
	void deleteOrder(const uint64_t&);
	// 将订单加入至待售卖容器
	void addOrderToSell(const std::string&, const uint64_t&);
	// 将订单加入至待购买容器
	void addOrderToBuy(const std::string&, const uint64_t&);
	// 市场价
	double current; 
	// 访问互斥锁
	std::mutex* process_mutex;
};

// 根据新订单请求做出应答消息
void OrderServiceImpl::processNewOrder(const NewOrderRequest& request, ExecutionReport& report){
	std::string errorMessage="";
	// 判断订单的合法性
	if(!checkRequest(request, errorMessage)){
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		return;
	}
	// 加锁
	process_mutex->lock();
	// 创建订单
	auto orderID=createOrder(request);
	auto stockID=request.stockid();
	// 记录成交的数量
	uint32_t cnt=0; 
	// 买或卖
	if(request.direction()==NewOrderRequest::SELL){ // 卖
		// 查看该股票是否有买订单
		if(!buy.count(stockID)||buy[stockID].size()==0){
			addOrderToSell(stockID, orderID);
		}else{
			auto& orderSet=buy[stockID];
			for(auto it=orderSet.begin(); it!=orderSet.end();){
				if(cnt>=request.orderqty()) break;
				auto buyOrderID=*it;
				auto& buyOrder=orders[buyOrderID];
				// 判断价格是否可以卖出
				if(request.ordertype()==NewOrderRequest::LIMIT){
					if(request.price()-MINN>buyOrder.price()) {
						it++;
						continue;
					}

				}else{
					if(current-MINN>buyOrder.price()) {
						it++;					
						continue;
					}
				}
				// 计算可卖出的数量
				auto tradNum=std::min(buyOrder.orderqty(), request.orderqty()-cnt);
				// 从数据库中修改订单的库存量
				auto num=buyOrder.orderqty();
				buyOrder.set_orderqty(num-tradNum);
				cnt+=tradNum;
				if(buyOrder.orderqty()==0){
					orders.erase(buyOrderID);
					orderSet.erase(it++);
				} 
				else {
					it++;
				}
			}
			if(cnt<request.orderqty()){
				auto cpy=request;
				cpy.set_orderqty(request.orderqty()-cnt);
				alterOrder(orderID, cpy);
				addOrderToSell(stockID, orderID);
			}else{
				deleteOrder(orderID);
			}
		}
		// 设置订单提交成功的应答
		report.set_stat(ExecutionReport::ORDER_ACCEPT);
		report.set_orderid(orderID);
		report.set_fillqty(cnt);
		report.set_leaveqty(request.orderqty()-cnt);
		report.set_time(getTime());
	}else{ // 买
		// 查看该股票是否有卖订单
		if(!sell.count(stockID)||sell[stockID].size()==0){
			addOrderToBuy(stockID, orderID);	
		}
		else{
			auto& orderSet=sell[stockID];
			for(auto it=orderSet.begin(); it!=orderSet.end();){
				if(cnt>=request.orderqty()) break;
				auto sellOrderID=*it;
				auto& sellOrder=orders[sellOrderID];
				// 判断价格是否可以购买
				if(sellOrder.ordertype()==NewOrderRequest::LIMIT){
					if(request.price()<sellOrder.price()-MINN) {
						it++;
						continue;
					}

				}else{
					if(request.price()<current-MINN) {
						it++;					
						continue;
					}
				}
				// 计算可购买的数量
				auto tradNum=std::min(sellOrder.orderqty(), request.orderqty()-cnt);
				// 从数据库中修改订单的库存量
				auto num=sellOrder.orderqty();
				sellOrder.set_orderqty(num-tradNum);
				cnt+=tradNum;
				if(sellOrder.orderqty()==0){
					orders.erase(sellOrderID);
					orderSet.erase(it++);
				} 
				else {
					it++;
				}
			}
			if(cnt<request.orderqty()){
				auto cpy=request;
				cpy.set_orderqty(request.orderqty()-cnt);
				alterOrder(orderID, cpy);
				addOrderToBuy(stockID, orderID);
			}else{
				deleteOrder(orderID);
			}
		}
		// 设置订单提交成功的应答
		report.set_stat(ExecutionReport::FILL);
		report.set_orderid(orderID);
		report.set_fillqty(cnt);
		report.set_leaveqty(request.orderqty()-cnt);
		report.set_time(getTime());	
	}
	// 解锁
	process_mutex->unlock();

}

// 根据撤销订单请求做出应答消息
void OrderServiceImpl::processCancelOrder(const CancelOrderRequest& request, ExecutionReport& report){
	std::string errorMessage="";
	// 加锁
	process_mutex->lock();
	if(!orders.count(request.orderid())){
		errorMessage="Error: Can not find OrderID!";
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		// 解锁
		process_mutex->unlock();
		return;	
	}
	auto order=orders[request.orderid()];
	auto stockID=order.stockid();
	if(order.direction()==NewOrderRequest::SELL){
		sell[stockID].erase(request.orderid());
	}else{
		buy[stockID].erase(request.orderid());
	}
	orders.erase(request.orderid());
	report.set_stat(ExecutionReport::CANCELED);
	report.set_clientid(order.clientid());
	report.set_stockid(stockID);
	report.set_orderqty(order.orderqty());
	report.set_orderprice(order.price());
	report.set_time(getTime());
	// 解锁
	process_mutex->unlock();
}

// 创建订单
uint64_t OrderServiceImpl::createOrder(const NewOrderRequest& request){
	int orderID=++id;
	orders[orderID]=request;
	std::string stockID=request.stockid();
	return orderID;
}

// 修改订单
void OrderServiceImpl::alterOrder(const uint64_t& orderID, const NewOrderRequest& request){
	orders[orderID]=request;
}

// 删除订单
void OrderServiceImpl::deleteOrder(const uint64_t& orderID){
	orders.erase(orderID);
}

// 将订单加入至待售卖容器
void OrderServiceImpl::addOrderToSell(const std::string& stockID, const uint64_t& orderID){
	sell[stockID].emplace(orderID);
}

// 将订单加入至待购买容器
void OrderServiceImpl::addOrderToBuy(const std::string& stockID, const uint64_t& orderID){
	buy[stockID].emplace(orderID);
}


// 运行服务端
void RunServer(){
	std::string server_address("0.0.0.0:50000");
	OrderServiceImpl service;
	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	builder.RegisterService(&service);
	std::unique_ptr<Server> server(builder.BuildAndStart());
	std::cout<<"Server listening on: "<<server_address<<std::endl;
	server->Wait();
}

int main(int argc, char** argv){
	RunServer();
	return 0;
}
