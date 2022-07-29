#include"async_client.h"

// 创建新订单请求
NewOrderRequest MakeNewOrderRequest(const bool& type, const bool& direction, 
				const uint64_t& clientID, const std::string& stockID,
				const uint32_t& orderQty, const double& price){
	NewOrderRequest request;
	if(type==TYPE_LIMIT) request.set_ordertype(NewOrderRequest::LIMIT);
	else request.set_ordertype(NewOrderRequest::MARKET);
	
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

// 创建查询订单请求
QueryOrderRequest MakeQueryOrderRequest(){
	QueryOrderRequest request;
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

// 撤销订单类
AsyncClientCallPushCancelOrder::AsyncClientCallPushCancelOrder(const CancelOrderRequest& request, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_):
	AbstractAsyncClientCall(){
	// responder=stub_->AsyncPushCancelOrder(&context, request, &cq_);
	responder=stub_->PrepareAsyncPushCancelOrder(&context, request, &cq_);
	responder->StartCall();
	responder->Finish(&report_, &status, (void*)this);
	callStatus=PROCESS;
}

void AsyncClientCallPushCancelOrder::Proceed(bool ok){
		if(callStatus==PROCESS){
			GPR_ASSERT(ok);
			if(status.ok())
				printReport(report_);
			// TODO
			delete this;
		}
}

// 提交订单类
AsyncClientCallPushNewOrder::AsyncClientCallPushNewOrder(std::vector<NewOrderRequest>&& requests, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_):
	AbstractAsyncClientCall(), requests_(requests), counter(0), writing_mode_(true){
	responder_=stub_->PrepareAsyncPushNewOrder(&context, &cq_);
	responder_->StartCall((void*)this);
	callStatus=PROCESS;
}

void AsyncClientCallPushNewOrder::Proceed(bool ok){
	// sleep(1);
	if(callStatus==PROCESS){
		if(writing_mode_){
			if(counter<requests_.size()){
				//std::cout<<"Writing request..."<<std::endl;	
				//printRequest(requests_[counter]);
				responder_->Write(requests_[counter], (void*)this);
				++counter;				
			}else{
				//std::cout<<"Writing done!"<<std::endl;
				responder_->WritesDone((void*)this);
				writing_mode_=false;	
			}
		}else{
			if(!ok){
				//std::cout<<"Read finish!"<<std::endl;
				// callStatus=FINISH;
				// responder_->Finish(&status, (void*)this);	
			}else{
				//std::cout<<"Reading report..."<<std::endl;
				responder_->Read(&report_, (void*)this);	
				if(report_.clientid()>0){
					printReport(report_);
				}
			}
		}
	}else if(callStatus==FINISH){
		std::cout<<"Delete!"<<std::endl;
		delete this;
	}
}

// 查询订单类
AsyncClientCallPushQueryOrder::AsyncClientCallPushQueryOrder(const QueryOrderRequest& request, CompletionQueue& cq_, std::unique_ptr<OrderService::Stub>& stub_):
	AbstractAsyncClientCall(), reportsCounter(0){
		responder = stub_->AsyncPushQueryOrder(&context, request, &cq_, (void*)this);
		callStatus = PROCESS ;
}

void AsyncClientCallPushQueryOrder::Proceed(bool ok){
	if(callStatus == PROCESS){
		if(!ok){
			responder->Finish(&status, (void*)this);
			callStatus = FINISH;
			if(reportsCounter==0){
				std::cout<<"无订单！"<<std::endl;
			}
			return ;
		}
		responder->Read(&queryReport_, (void*)this);
		if(queryReport_.clientid()>0) {
			printReport(queryReport_);
			reportsCounter++;
		}
	}
	else if(callStatus == FINISH){
			delete this;
	}
}

// 客户端类
OPSClient::OPSClient(std::shared_ptr<Channel> channel):
		stub_(OrderService::NewStub(channel)){}

// 提交订单
void OPSClient::PushNewOrder(const std::string& fileName){
	std::vector<NewOrderRequest> requests;
	readNewOrderRequest(fileName, requests);
	// 注册报单请求处理
	new AsyncClientCallPushNewOrder(std::move(requests), cq_, stub_);
}

// 删除订单
void OPSClient::PushCancelOrder(const uint64_t& orderID){
	CancelOrderRequest request=MakeCancelOrderRequest(orderID);
	// 注册撤单请求处理
	new AsyncClientCallPushCancelOrder(request, cq_, stub_);
}

// 查询订单
void OPSClient::PushQueryOrder(){
	QueryOrderRequest request=MakeQueryOrderRequest();
	// 注册查询订单请求
	new AsyncClientCallPushQueryOrder(request, cq_, stub_);
}

// 异步处理完成队列中的事件
void OPSClient::AsyncCompleteRpc(){
	void* got_tag;
	bool ok=false;
	// 从完成队列中取出请求处理
	while(cq_.Next(&got_tag, &ok)){
		// 基类指针,根据子类执行的虚函数Proceed()
		AbstractAsyncClientCall* call=static_cast<AbstractAsyncClientCall*>(got_tag);
		call->Proceed(ok);
	}
}

int main(int argc, char* argv[]){
	OPSClient client(grpc::CreateChannel("localhost:50010", grpc::InsecureChannelCredentials()));
	std::thread thread_=std::thread(&OPSClient::AsyncCompleteRpc, &client);
	std::cout<<"Please input operator and requests! usage: <New/ Cancel> <RequestsFile/ orderID>"<<std::endl;
	while(1){
		std::string op;
		std::cin>>op;
		if(op=="New"||op=="N"||op=="new"||op=="n"){
			std::string fileName;
			std::cin>>fileName;
			client.PushNewOrder(fileName);
		}else if(op=="Cancel"||op=="C"||op=="cancel"||op=="c"){
			uint64_t orderID;
			std::cin>>orderID;
			client.PushCancelOrder(orderID);
		}else if(op=="Query"||op=="Q"||op=="query"||op=="q"){
			client.PushQueryOrder();
		}
	}
	thread_.join();
	return 0;
}
