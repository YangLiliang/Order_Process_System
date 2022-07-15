#include "helper.h"

// 获取时间
std::string getTime(){
	std::time_t cur;
	time(&cur);
	std::string time_str=ctime(&cur);
	return time_str;
}

void printRequest(const NewOrderRequest& request){
	std::cout<<"New order request {"<<std::endl;
	std::cout<<"	[ClientID] "<<request.clientid()<<", "<<std::endl;
	if(request.direction()==NewOrderRequest::SELL) std::cout<<"	[SELL]"<<", "<<std::endl;
	else std::cout<<"	[BUY]"<<", "<<std::endl;
	std::cout<<"	[StockID] "<<request.stockid()<<", "<<std::endl;
	std::cout<<"	[Order Quantity] "<<request.orderqty()<<", "<<std::endl;
	std::cout<<"	[Price] "<<request.price()<<", "<<std::endl;
	if(request.ordertype()==NewOrderRequest::LIMIT) std::cout<<"	[LIMIT]"<<", "<<std::endl;
	else std::cout<<"	[CURRENT]"<<", "<<std::endl;
	std::cout<<"	[Time] "<<request.time();
	std::cout<<"}"<<std::endl;
}

void printRequest(const CancelOrderRequest& request){
	std::cout<<"Cancel order request {"<<std::endl;
	std::cout<<"	[OrderID] "<<request.orderid()<<", "<<std::endl;
	std::cout<<"	[Time] "<<request.time();
	std::cout<<"}"<<std::endl;
}

void printReport(const ExecutionReport& report){
	std::cout<<"Execution report {"<<std::endl;
	if(report.stat()==ExecutionReport::ORDER_ACCEPT){
		std::cout<<"	[ORDER_ACCEPT]"<<", "<<std::endl;
	}else if(report.stat()==ExecutionReport::ORDER_REJECT){
		std::cout<<"	[ORDER_REJECT]"<<", "<<std::endl;
	}else if(report.stat()==ExecutionReport::FILL){
		std::cout<<"	[FILL]"<<", "<<std::endl;
	}else if(report.stat()==ExecutionReport::CANCELED){
		std::cout<<"	[CANCELED]"<<", "<<std::endl;
	}else{
		std::cout<<"	[CANCEL_REJECT]"<<", "<<std::endl;
	}
	std::cout<<"	[ClientID] "<<report.clientid()<<", "<<std::endl;
	std::cout<<"	[OrderID] "<<report.orderid()<<", "<<std::endl;
	std::cout<<"	[StockID] "<<report.stockid()<<", "<<std::endl;
	std::cout<<"	[Order Quantity] "<<report.orderqty()<<", "<<std::endl;
	std::cout<<"	[Order Price] "<<report.orderprice()<<", "<<std::endl;
	std::cout<<"	[Fill Quantity] "<<report.fillqty()<<", "<<std::endl;
	std::cout<<"	[Fill Price] "<<report.fillprice()<<", "<<std::endl;
	std::cout<<"	[Leave Quantity] "<<report.leaveqty()<<", "<<std::endl;
	std::cout<<"	[Error Message] "<<report.errormessage()<<", "<<std::endl;
	std::cout<<"	[Time] "<<report.time();
	std::cout<<"}"<<std::endl;
}

// 判断订单的合法性
bool checkRequest(const NewOrderRequest& request, std::string& errorMessage){
	if(request.clientid()<=0){
		errorMessage="Error: ClientID is illegal!";
	}else if(request.direction()!=NewOrderRequest::SELL&&request.direction()!=NewOrderRequest::BUY){
		errorMessage="Error: Order direction is illegal!";
	}else if(request.orderqty()<=0){
		errorMessage="Error: Order quantity is illegal!";
	}else if(request.price()<=0){
		errorMessage="Error: Order price is illegal!";
	}else if(request.ordertype()!=NewOrderRequest::LIMIT&&request.ordertype()!=NewOrderRequest::MARKET){
		errorMessage="Error: Order type is illegal!";
	}
	if(errorMessage.size()>0) return false;
	return true;
}

// 初始化应答
void initReport(ExecutionReport& report, const NewOrderRequest& request){
	// 订单状态
	report.set_stat(ExecutionReport::ORDER_REJECT);
	// 客户ID
	report.set_clientid(request.clientid());
	// 订单ID
	report.set_orderid(0);
	// 股票代码
	report.set_stockid(request.stockid());
	// 订单总量
	report.set_orderqty(request.orderqty());
	// 订单价格
	report.set_orderprice(request.price());
	// 订单成交数量
	report.set_fillqty(0);
	// 订单成交价格
	report.set_fillprice(0);
	// 剩余待成交数量
	report.set_leaveqty(request.orderqty());
	// 错误信息
	report.set_errormessage("");
	// 时间
	report.set_time("");
}
// 初始化应答
void initReport(ExecutionReport& report, const CancelOrderRequest& request){
	report.set_stat(ExecutionReport::CANCEL_REJECT);
	report.set_clientid(0);
	report.set_orderid(request.orderid());
	report.set_stockid("");
	report.set_orderqty(0);
	report.set_orderprice(0);
	report.set_fillqty(0);
	report.set_fillprice(0);
	report.set_leaveqty(0);
	report.set_errormessage("");
	report.set_time("");
}
