#include"async_server.h"

// 处理新订单类
CallDataPushNewOrder::CallDataPushNewOrder(OrderService::AsyncService* service, ServerCompletionQueue* cq, TradingMarket* tradingMarket):
		CommonCallData(service, cq, tradingMarket), responder_(&ctx_), new_responder_created(false), writing_mode_(false), RequestsCounter(0), ReportsCounter(0){
	Proceed();
}

void CallDataPushNewOrder::Proceed(bool ok) {
	if(status_==CREATE){
		std::cout<<"Create call data!"<<std::endl;
		status_=PROCESS;
		service_->RequestPushNewOrder(&ctx_, &responder_, cq_, cq_, (void*)this);	
	}else if(status_==PROCESS){
		if(!new_responder_created){
			new CallDataPushNewOrder(service_, cq_, tradingMarket_);
			new_responder_created=true;
		}
		if(!writing_mode_){
			if(!ok){
				std::cout<<"Reading finish!"<<std::endl;
				writing_mode_=true;
				ok=true;	
			}else{
				std::cout<<"Reading request..."<<std::endl;
				responder_.Read(&newOrderRequest_, (void*)this);
				printRequest(newOrderRequest_);
				if(newOrderRequest_.clientid()>0){
					uint64_t orderID=0;
					tradingMarket_->processNewOrder(newOrderRequest_, reports, orderID);
					if(orderID>0){
						(orderID_responder)[orderID]=&responder_;
					}
				}
			}		
		}
		if(writing_mode_){
			if(!ok||ReportsCounter>=reports.size()){
				std::cout<<"Writing done!"<<std::endl;
				//status_=FINISH;
				//responder_.Finish(Status(), (void*)this);	
			}else{
				std::cout<<"Writing report..."<<std::endl;
				auto& orderID= reports[ReportsCounter].first;
				auto& report=reports[ReportsCounter].second;
				printReport(report);
				if(orderID==0){
					responder_.Write(report, (void*)this);
				}else{
					orderID_responder[orderID]->Write(report, (void*)this);
				}
				++ReportsCounter;
			}
		}
	}else{
		std::cout<<"Delete!"<<std::endl;
		// delete this;
	}	
}

// 处理撤销订单
CallDataPushCancelOrder::CallDataPushCancelOrder(OrderService::AsyncService* service, ServerCompletionQueue* cq, TradingMarket* tradingMarket):
		CommonCallData(service, cq, tradingMarket), responder_(&ctx_){
	Proceed();
}
void CallDataPushCancelOrder::Proceed(bool ok) {
	if(status_==CREATE){
		status_=PROCESS;
		service_->RequestPushCancelOrder(&ctx_, &cancelOrderRequest_, &responder_, cq_, cq_, this);	
	}else if(status_==PROCESS){
		new CallDataPushCancelOrder(service_, cq_, tradingMarket_);
		printRequest(cancelOrderRequest_);
		initReport(report_, cancelOrderRequest_);
		tradingMarket_->processCancelOrder(cancelOrderRequest_,  report_);
		printReport(report_);
		status_=FINISH;
		responder_.Finish(report_, Status::OK, this);
	}else{
		GPR_ASSERT(status_==FINISH);
		delete this;
	}
}

// 服务端类
void ServerImpl::Run(){
	std::string server_address("0.0.0.0:50002");
	ServerBuilder builder;
	builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
	// 注册服务
	builder.RegisterService(&service_);
	// 建立完成队列
	cq_=builder.AddCompletionQueue();
	server_=builder.BuildAndStart();
	std::cout<<"Server listening on: "<<server_address<<std::endl;	
	// 注册请求处理
	new CallDataPushNewOrder(&service_, cq_.get(), tradingMarket);
	new CallDataPushCancelOrder(&service_, cq_.get(), tradingMarket);
	void* tag;
	bool ok;
	// 从完成队列中取出请求处理
	while(true){
		// 当WriteDone时ok为0
		GPR_ASSERT(cq_->Next(&tag, &ok));
		// 基类指针,根据子类执行的虚函数Proceed()
		CommonCallData* calldata=static_cast<CommonCallData*>(tag);
		calldata->Proceed(ok);
	}
}

int main(int argc, char** argv) {
  ServerImpl server;
  server.Run();
  return 0;
}
