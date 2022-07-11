#include <string>
#include <memory>
#include <thread>
#include <time.h>
#include <unistd.h>
#include <fstream>
#include "../helper/helper.h"

#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>
#include "../proto/OrderProcessSystem.grpc.pb.h"

#define TYPE_LIMIT true
#define TYPE_CURRENT false
#define DIRE_SELL true
#define DIRE_BUY false

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReader;
using grpc::ClientReaderWriter;
using grpc::ClientWriter;
using grpc::Status;

using OPS::NewOrderRequest;
using OPS::CancelOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderService;

// 创建新订单请求
NewOrderRequest MakeNewOrderRequest(const bool& type, const bool& direction, 
				const uint64_t& clientID, const std::string& stockID,
				const uint32_t& orderQty, const double& price){
	NewOrderRequest request;
	if(type==TYPE_LIMIT) request.set_ordertype(NewOrderRequest::LIMIT);
	else request.set_ordertype(NewOrderRequest::CURRENT);
	
	if(direction==DIRE_SELL) request.set_direction(NewOrderRequest::SELL);
	else request.set_direction(NewOrderRequest::BUY);
	
	request.set_clientid(clientID);
	request.set_stockid(stockID);
	request.set_orderqty(orderQty);
	request.set_price(price);
	request.set_time(getTime());
	return request;
}

// 创建撤销订单请求
CancelOrderRequest MakeCancelOrderRequest(const uint64_t& orderID){
	CancelOrderRequest request;
	request.set_orderid(orderID);
	request.set_time(getTime());
	return request;
}

// 读入新订单文件
void readNewOrderRequest(const std::string& fileName, std::vector<NewOrderRequest>& requests){
	std::ifstream fin;
	fin.open(fileName);
	int requestNum;
	fin>>requestNum;
	std::string type, direction, stockID;
	uint64_t clientID;
	uint32_t orderQty;
	double price;
	for(int i=0;i<requestNum;i++){
		fin>>type>>direction>>clientID>>stockID>>orderQty>>price;
		NewOrderRequest request=MakeNewOrderRequest(type=="LIMIT" ? true: false, 
							direction=="SELL" ? true: false, 
							clientID, stockID, orderQty, price);
		requests.push_back(std::move(request));
	}
}

// 客户端类：订单服务
class OrderServiceClient{
public:
	OrderServiceClient(std::shared_ptr<Channel> channel):stub_(OrderService::NewStub(channel)){}
	
	// 报单
	void PushNewOrder(const std::string& NewOrderFileName){
		ClientContext context;
		std::shared_ptr<ClientReaderWriter<NewOrderRequest, ExecutionReport> > stream(stub_->PushNewOrder(&context));
		std::thread writer([stream](const std::string& NewOrderFileName){
			std::vector<NewOrderRequest> requests;
			readNewOrderRequest(NewOrderFileName, requests);
			for(const auto&request:requests){
				//std::cout<<"send message"<<std::endl;
				printRequest(request);
				stream->Write(request);	
				//sleep(1);		
			}
			stream->WritesDone();
		}, NewOrderFileName);
		ExecutionReport report;
		while(stream->Read(&report)){
			printReport(report);
		}
		writer.join();
		Status status=stream->Finish();
		if(!status.ok()){
			std::cout<<"RPC Failed!"<<std::endl;
		}
	}
	
	// 撤单
	void PushCancelOrder(const uint64_t& orderID){
		ClientContext context;
		CancelOrderRequest request;
		ExecutionReport report;
		request=MakeCancelOrderRequest(orderID);
		printRequest(request);
		Status status=stub_->PushCancelOrder(&context, request, &report);
		printReport(report);
		if(!status.ok()){
			std::cout<<"RPC Failed!"<<std::endl;	
		}
	}
private:
	std::unique_ptr<OrderService::Stub> stub_;
};
int main(int argc, char** argv){
	if(argc!=3){
		std::cout<<"Error: usage: ./OPSClient <N/C> <FILE/ORDERID>"<<std::endl;
		return 0;	
	}
	std::cout<<argv[1]<<std::endl;
	if(argv[1][0]=='N'){
		std::string NewOrderFileName=argv[2];
		OrderServiceClient guide(grpc::CreateChannel("0.0.0.0:50000",grpc::InsecureChannelCredentials()));
		guide.PushNewOrder(NewOrderFileName);
	}else if(argv[1][0]=='C'){
		uint64_t orderID=atoi(argv[2]);
		OrderServiceClient guide(grpc::CreateChannel("0.0.0.0:50000",grpc::InsecureChannelCredentials()));
		guide.PushCancelOrder(orderID);
	}else{
		std::cout<<"Error: usage: ./OPSClient <NEW/CANCEL> <FILE/ORDERID>"<<std::endl;
	}
	return 0;
}
