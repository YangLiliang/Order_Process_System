#include "market.h"
// 根据新订单请求做出应答消息
void TradingMarket::processNewOrder(const NewOrderRequest& request, ServerReaderWriter<ExecutionReport, NewOrderRequest>* stream){
	std::string errorMessage="";
	// 初始化应答
	ExecutionReport report;
	initReport(report, request);
	// 判断订单的合法性
	if(!checkRequest(request, errorMessage)){
		// 非法订单输出报错信息
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		stream->Write(report);
		return;
	}
	// 创建订单
	auto orderID=createOrder(request);
	// 存储订单对应的stream
	orderID_stream[orderID]=stream;
	// 输出订单创建成功的消息
	report.set_stat(ExecutionReport::ORDER_ACCEPT);
	report.set_orderid(orderID);
	report.set_time(getTime());
	stream->Write(report);
	// 获取订单对应的股票ID
	auto stockID=request.stockid();
	// Sell
	if(request.direction()==NewOrderRequest::SELL){
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].second);
		// 存在该股票且订单数不为0, 搜索买订单
		if(sell_buy_containers.count(stockID)&&sell_buy_containers[stockID].buy.size()>0){
			sellOrders(orderID, stockID);
		}
	// Buy
	}else{
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].first);
		// 存在该股票且订单数不为0, 搜索卖订单
		if(sell_buy_containers.count(stockID)&&sell_buy_containers[stockID].sell.size()>0){
			buyOrders(orderID, stockID);	
		}
	}
	// 分开的缺点：order不能及时插入容器中
	// Sell
	if(request.direction()==NewOrderRequest::SELL){
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].first);
		// 剩余待售卖订单数不为0，加入sell集合
		if(orders[orderID].orderqty()>0){
			addOrderToSell(stockID, orderID);
		}
	// Buy
	}else{
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].second);
		// 剩余待购买订单数不为0，加入buy集合
		if(orders[orderID].orderqty()>0){
			addOrderToBuy(stockID, orderID);
		}
	}
}

// 根据撤销订单请求做出应答消息
void TradingMarket::processCancelOrder(const CancelOrderRequest& request, ExecutionReport& report){
	std::string errorMessage="";
	// 对orders的删除操作加锁，作用域结束自动解锁
	std::lock_guard<std::mutex> l1(cancel_mutex);
	// 判断orderID是否存在
	if(!orders.count(request.orderid())){
		errorMessage="Error: Can not find OrderID!";
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		return;	
	}
	uint64_t orderID=request.orderid();
	auto order=orders[orderID];
	auto stockID=order.stockid();
	if(order.direction()==NewOrderRequest::SELL){
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].first);
		// 从卖集合容器中删除订单
		if(sell_buy_containers[stockID].sell.count(orderID))
			sell_buy_containers[stockID].sell.erase(orderID);
		// 从订单容器中删除订单
		deleteOrder(orderID);
		// 删除订单及其对应的流
		orderID_stream.erase(orderID);
	}else{
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::lock_guard<std::mutex> lg(*stock_mutex[stockID].second);
		// 从买集合容器中删除订单
		if(sell_buy_containers[stockID].buy.count(orderID))
			sell_buy_containers[stockID].buy.erase(orderID);
		// 从订单容器中删除订单
		deleteOrder(orderID);
		// 删除订单及其对应的流
		orderID_stream.erase(orderID);
	}
	report.set_stat(ExecutionReport::CANCELED);
	report.set_clientid(order.clientid());
	report.set_stockid(stockID);
	report.set_orderqty(order.orderqty());
	report.set_orderprice(order.price());
	report.set_time(getTime());
}

// 卖订单操作
void TradingMarket::sellOrders(const uint64_t& orderID, const std::string& stockID){
	// 获取卖订单
	NewOrderRequest& sellOrder=orders[orderID];
	// 记录成交的数量
	uint32_t cnt=0; 
	// 买订单容器
	auto& orderSet=sell_buy_containers[stockID].buy;
	// 遍历容器
	for(auto it=orderSet.begin(); it!=orderSet.end();){
		if(cnt>=sellOrder.orderqty()) break;
		auto buyOrderID=*it;
		auto& buyOrder=orders[buyOrderID];
		// 不能与同一用户发布的订单进行交易
		if(sellOrder.clientid()==buyOrder.clientid()){
			it++;
			continue;	
		}
		// 判断价格是否可以卖出 TODO: 优化价格判断
		if(sellOrder.price()-MINN>buyOrder.price()){
			it++;
			continue;
		}
		double fillPrice=buyOrder.price();
		// 计算可卖出的数量
		auto tradNum=std::min(buyOrder.orderqty(), sellOrder.orderqty()-cnt);
		cnt+=tradNum;
		// 设置当前订单交易成功的应答
		ExecutionReport report;
		initReport(report, sellOrder);
		report.set_stat(ExecutionReport::FILL);
		report.set_orderid(orderID);
		report.set_fillqty(tradNum);
		report.set_fillprice(fillPrice);
		report.set_leaveqty(sellOrder.orderqty()-cnt);
		report.set_time(getTime());
		// 设置buy订单交易成功的应答
		ExecutionReport report_;
		initReport(report_, buyOrder);
		report_.set_stat(ExecutionReport::FILL);
		report_.set_orderid(buyOrderID);
		report_.set_fillqty(tradNum);
		report_.set_fillprice(fillPrice);
		report_.set_leaveqty(buyOrder.orderqty()-tradNum);
		report_.set_time(getTime());
		// 从数据库中修改buy订单的库存量
		auto num=buyOrder.orderqty();
		buyOrder.set_orderqty(num-tradNum);
		// 获取buy和sell order的stream
		auto& sellOrder_stream=orderID_stream[orderID];
		auto& buyOrder_stream=orderID_stream[buyOrderID];
		// 发出report
		sellOrder_stream->Write(report);
		buyOrder_stream->Write(report_);
		// 判断订单的数量是否大于0
		if(buyOrder.orderqty()==0){
			orderSet.erase(it++);
		} 
		else {
			it++;
		}
	}
	// 修改当前订单的数量
	auto sellOrderQty=sellOrder.orderqty();
	sellOrder.set_orderqty(sellOrderQty-cnt);
	// 修改市价单的价格为市价
	if(sellOrder.ordertype()==NewOrderRequest::MARKET){
		sellOrder.set_price(market);
	}
}

// 买订单操作
void TradingMarket::buyOrders(const uint64_t& orderID, const std::string& stockID){
	// 获取买订单
	NewOrderRequest& buyOrder=orders[orderID];
	// 记录成交的数量
	uint32_t cnt=0; 
	// 卖订单容器
	auto& orderSet=sell_buy_containers[stockID].sell;
	// 遍历容器
	for(auto it=orderSet.begin(); it!=orderSet.end();){
		if(cnt>=buyOrder.orderqty()) break;
		auto sellOrderID=*it;
		auto& sellOrder=orders[sellOrderID];
		// 不能与同一用户发布的订单进行交易
		if(buyOrder.clientid()==sellOrder.clientid()){
			it++;
			continue;	
		}
		// 判断价格是否可以购买
		if(buyOrder.price()<sellOrder.price()-MINN){
			it++;
			continue;
		}
		double fillPrice=sellOrder.price();
		// 计算可购买的数量
		auto tradNum=std::min(sellOrder.orderqty(), buyOrder.orderqty()-cnt);
		cnt+=tradNum;
		// 设置当前订单交易成功的应答
		ExecutionReport report;
		initReport(report, buyOrder);
		report.set_stat(ExecutionReport::FILL);
		report.set_orderid(orderID);
		report.set_fillqty(tradNum);
		report.set_fillprice(fillPrice);
		report.set_leaveqty(buyOrder.orderqty()-cnt);
		report.set_time(getTime());
		// 设置sell订单交易成功的应答
		ExecutionReport report_;
		initReport(report_, sellOrder);
		report_.set_stat(ExecutionReport::FILL);
		report_.set_orderid(sellOrderID);
		report_.set_fillqty(tradNum);
		report_.set_fillprice(fillPrice);
		report_.set_leaveqty(sellOrder.orderqty()-tradNum);
		report_.set_time(getTime());
		// 从数据库中修改订单的库存量
		auto num=sellOrder.orderqty();
		sellOrder.set_orderqty(num-tradNum);
		// 获取buy和sell order的stream
		auto& buyOrder_stream=orderID_stream[orderID];
		auto& sellOrder_stream=orderID_stream[sellOrderID];
		// 发送report
		buyOrder_stream->Write(report);
		sellOrder_stream->Write(report_);
		// 判断订单的数量是否大于0
		if(sellOrder.orderqty()==0){
			orderSet.erase(it++);
		} 
		else {
			it++;
		}
	}
	// 修改当前订单的数量
	auto orderQty=buyOrder.orderqty();
	buyOrder.set_orderqty(orderQty-cnt);
	// 修改市价单的价格
	if(buyOrder.ordertype()==NewOrderRequest::MARKET){
		buyOrder.set_price(market);
	}
}

// 创建订单
uint64_t TradingMarket::createOrder(const NewOrderRequest& request){
	// 加锁，作用域结束自动解锁
	std::lock_guard<std::mutex> lg(orderID_mutex);
	// 为订单分配ID
	uint64_t orderID=++id;
	orders[orderID]=request;
	std::string stockID=request.stockid();
	// 为stockID分配锁
	if(!stock_mutex.count(stockID)){
		std::pair<std::mutex*, std::mutex*> sellAndBuyMutex;
		sellAndBuyMutex.first=new std::mutex();
		sellAndBuyMutex.second=new std::mutex();
		stock_mutex.insert(std::make_pair(stockID, sellAndBuyMutex));
	}
	return orderID;
}

// 修改订单
void TradingMarket::alterOrder(const uint64_t& orderID, const NewOrderRequest& request){
	orders[orderID]=request;
}

// 删除订单
void TradingMarket::deleteOrder(const uint64_t& orderID){
	orders.erase(orderID);
}

// 将订单加入至待售卖容器
void TradingMarket::addOrderToSell(const std::string& stockID, const uint64_t& orderID){
	sell_buy_containers[stockID].sell.emplace(orderID);
}

// 将订单加入至待购买容器
void TradingMarket::addOrderToBuy(const std::string& stockID, const uint64_t& orderID){
	sell_buy_containers[stockID].buy.emplace(orderID);
}
