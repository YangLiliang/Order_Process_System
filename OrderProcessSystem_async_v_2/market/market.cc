#ifndef MARKET_CC
#define MARKET_CC
#include "market.h"

TradingMarket* TradingMarket::m_instance=new TradingMarket;

// 根据新订单请求做出应答消息
void TradingMarket::processNewOrder(const NewOrderRequest& request, std::vector<std::pair<uint64_t, ExecutionReport> >& reports, uint64_t& orderID_){
	// 错误信息
	std::string errorMessage="";
	// 初始化应答
	ExecutionReport report;
	initReport(report, request);
	// 判断订单的合法性
	if(!checkRequest(request, errorMessage)){
		// 非法订单输出报错信息
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		reports.push_back(std::make_pair(0, report));
		return;
	}
	// 创建订单
	auto orderID=createOrder(request);
	// 将订单ID返回给服务器
	orderID_=orderID;
	// 输出订单创建成功的消息
	report.set_stat(ExecutionReport::ORDER_ACCEPT);
	report.set_orderid(orderID);
	report.set_time(getTime());
	reports.push_back(std::make_pair(orderID, report));
	// 获取订单对应的股票ID
	auto stockID=request.stockid();
	// Sell
	if(request.direction()==NewOrderRequest::SELL){
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lg(*stock_mutex[stockID].second);
		// 存在该股票且订单数不为0, 搜索买订单
		if(existInContainers(stockID)&&getBuyOrderSet(stockID).size()>0){
			sellOrders(orderID, stockID, reports);
		}
	// Buy
	}else{
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lg(*stock_mutex[stockID].first);
		// 存在该股票且订单数不为0, 搜索卖订单
		if(existInContainers(stockID)&&getSellOrderSet(stockID).size()>0){
			buyOrders(orderID, stockID, reports);	
		}
	}
	// 分开的缺点：order不能及时插入容器中
	// Sell
	if(request.direction()==NewOrderRequest::SELL){
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lg(*stock_mutex[stockID].first);
		// 剩余待售卖订单数不为0, 加入sell集合, 否则从订单集合中删除该订单
		if(selectOrder(orderID).orderqty()>0){
			addOrderToSell(stockID, orderID);
		}else{
			deleteOrder(orderID);
		}
	// Buy
	}else{
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lg(*stock_mutex[stockID].second);
		// 剩余待购买订单数不为0, 加入buy集合, 否则从订单集合中删除该订单
		if(selectOrder(orderID).orderqty()>0){
			addOrderToBuy(stockID, orderID);
		}else{
			deleteOrder(orderID);
		}
	}
}

// 根据撤销订单请求做出应答消息
void TradingMarket::processCancelOrder(const CancelOrderRequest& request, ExecutionReport& report){
	// 错误信息
	std::string errorMessage="";
	uint64_t orderID=request.orderid();;
	NewOrderRequest order;
	std::string stockID;
	// 获取order
	if(!isExistAndGetOrder(orderID, order)){
		errorMessage="Error: Can not find OrderID!";
		report.set_time(getTime());
		report.set_errormessage(errorMessage);
		return;	
	}
	stockID=order.stockid();
	
	if(order.direction()==NewOrderRequest::SELL){
		// 对stockID的卖订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lk(*stock_mutex[stockID].first);
		// 从订单容器中删除订单
		if(!deleteOrder(orderID)){
			errorMessage="Error: Can not find OrderID!";
			report.set_time(getTime());
			report.set_errormessage(errorMessage);
			return;	
		}
		// 从卖集合容器中删除订单
		delOrderFromSell(stockID, orderID);
	}else{
		// 对stockID的买订单集合加锁,作用域结束自动解锁
		std::unique_lock<std::mutex> lk(*stock_mutex[stockID].second);
		// 从订单容器中删除订单
		if(!deleteOrder(orderID)){
			errorMessage="Error: Can not find OrderID!";
			report.set_time(getTime());
			report.set_errormessage(errorMessage);
			return;	
		}
		// 从买集合容器中删除订单
		delOrderFromBuy(stockID, orderID);
	}
	
	report.set_stat(ExecutionReport::CANCELED);
	report.set_clientid(order.clientid());
	report.set_stockid(stockID);
	report.set_orderqty(order.orderqty());
	report.set_orderprice(order.price());
	report.set_leaveqty(order.orderqty());
	report.set_time(getTime());
}

// 根据查询订单请求做出应答消息
void TradingMarket::processQueryOrder(const QueryOrderRequest& request, std::vector<OrderReport>& reports){
	getAllOrders(reports);
	std::sort(reports.begin(), reports.end(), [&](const OrderReport& a, const OrderReport& b){return a.orderid()<b.orderid();});
}

// 卖订单操作
void TradingMarket::sellOrders(const uint64_t& orderID, const std::string& stockID, 
		std::vector<std::pair<uint64_t, ExecutionReport> >& reports){
	// 获取卖订单
	NewOrderRequest sellOrder=selectOrder(orderID);
	// 记录成交的数量
	uint32_t cnt=0; 
	{
		// 买订单容器
		auto& orderSet=getBuyOrderSet(stockID);
		// 遍历容器
		for(auto it=orderSet.begin(); it!=orderSet.end();){
			if(cnt>=sellOrder.orderqty()) break;
			auto buyOrderID=*it;
			auto buyOrder=selectOrder(buyOrderID);
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
			alterOrder(buyOrderID, buyOrder);
			// 获取buy和sell order的stream
			// auto& sellOrder_stream=orderID_stream[orderID];
			// auto& buyOrder_stream=orderID_stream[buyOrderID];
			// 发出report
			reports.push_back(std::make_pair(orderID, report));
			reports.push_back(std::make_pair(buyOrderID, report_));
			//sellOrder_stream->Write(report);
			//buyOrder_stream->Write(report_);
			// 判断订单的数量是否大于0
			if(buyOrder.orderqty()==0){
				orderSet.erase(it++);
				deleteOrder(buyOrderID);
			} 
			else {
				it++;
			}
		}
	}
	// 修改当前订单的数量
	auto sellOrderQty=sellOrder.orderqty();
	sellOrder.set_orderqty(sellOrderQty-cnt);
	// 修改市价单的价格为市价
	if(sellOrder.ordertype()==NewOrderRequest::MARKET){
		sellOrder.set_price(market);
	}
	// 修改当前订单信息
	alterOrder(orderID, sellOrder);
}

// 买订单操作
void TradingMarket::buyOrders(const uint64_t& orderID, const std::string& stockID, 
		std::vector<std::pair<uint64_t, ExecutionReport> >& reports){
	// 获取买订单
	NewOrderRequest buyOrder=selectOrder(orderID);
	// 记录成交的数量
	uint32_t cnt=0; 
	{
		// 卖订单容器
		auto& orderSet=getSellOrderSet(stockID);
		// 遍历容器
		for(auto it=orderSet.begin(); it!=orderSet.end();){
			if(cnt>=buyOrder.orderqty()) break;
			auto sellOrderID=*it;
			auto sellOrder=selectOrder(sellOrderID);
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
			alterOrder(sellOrderID, sellOrder);
			// 获取buy和sell order的stream
			//auto& buyOrder_stream=orderID_stream[orderID];
			//auto& sellOrder_stream=orderID_stream[sellOrderID];
			// 发送report
			reports.push_back(std::make_pair(orderID, report));
			reports.push_back(std::make_pair(sellOrderID, report_));
			//buyOrder_stream->Write(report);
			//sellOrder_stream->Write(report_);
			// 判断订单的数量是否大于0
			if(sellOrder.orderqty()==0){
				orderSet.erase(it++);
				deleteOrder(sellOrderID);
			} 
			else {
				it++;
			}
		}
	}
	// 修改当前订单的数量
	auto orderQty=buyOrder.orderqty();
	buyOrder.set_orderqty(orderQty-cnt);
	// 修改市价单的价格
	if(buyOrder.ordertype()==NewOrderRequest::MARKET){
		buyOrder.set_price(market);
	}
	// 修改当前订单信息
	alterOrder(orderID, buyOrder);
}

// 创建订单
uint64_t TradingMarket::createOrder(const NewOrderRequest& request){
	// 为订单分配ID
	uint64_t orderID;
	{
		// 加锁,保护订单编号动态增加,作用域结束自动解锁
		std::unique_lock<std::mutex> lk(orderID_mutex);
		orderID=++id;
	}
	// 将订单存入订单集合中
	{
		insertOrder(orderID, request);
	}

	// 获取订单对应的股票ID
	std::string stockID=request.stockid();
	// 为stockID分配容器对象和锁
	if(!existInContainers(stockID)){
		insertStock(stockID);
	}
	return orderID;
}

/***************************************************************************************
                                 订单容器操作相关
****************************************************************************************/
// 插入新订单
void TradingMarket::insertOrder(const uint64_t& orderID, const NewOrderRequest& request){
	// 加锁,保护hash表的增删
	std::unique_lock<std::shared_mutex> w(rw_orders_mutex);
	orders.insert(std::make_pair(orderID, request));
}

// 修改订单
void TradingMarket::alterOrder(const uint64_t& orderID, const NewOrderRequest& request){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_orders_mutex);
	orders.at(orderID)=request;
}

// 删除订单
bool TradingMarket::deleteOrder(const uint64_t& orderID){
	// 加锁,保护hash表的增删
	std::unique_lock<std::shared_mutex> w(rw_orders_mutex);
	if(orders.find(orderID)!=orders.end()){
		orders.erase(orderID);
		return true;
	}
	return false;
}

// 查询订单
NewOrderRequest TradingMarket::selectOrder(const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_orders_mutex);
	return orders.at(orderID);
}

// 判断订单存在
bool TradingMarket::existInOrders(const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_orders_mutex);
	if(orders.find(orderID)!=orders.end()){
		return true;
	}
	return false;
}

// 判断订单存在且获取订单
bool TradingMarket::isExistAndGetOrder(const uint64_t& orderID, NewOrderRequest& order){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_orders_mutex);
	if(orders.find(orderID)!=orders.end()){
		order=orders.at(orderID);
		return true;
	}
	return false;
}

// 获取所有订单
void TradingMarket::getAllOrders(std::vector<OrderReport>& reports){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_orders_mutex);
	reports.reserve(orders.size());
	for(const auto& [orderID, order]:orders){
		OrderReport report;
		initReport(report, order, orderID);
		reports.push_back(report);
	}
}

/***************************************************************************************
                                    股票容器操作相关
****************************************************************************************/

// 插入新股票
void TradingMarket::insertStock(const std::string& stockID){
	// 写锁
	std::unique_lock<std::shared_mutex> w(rw_stocks_mutex);
	if(sell_buy_containers.find(stockID)==sell_buy_containers.end()){
		std::pair<std::mutex*, std::mutex*> sellAndBuyMutex;
		sellAndBuyMutex.first=new std::mutex();
		sellAndBuyMutex.second=new std::mutex();
		stock_mutex.insert(std::make_pair(stockID, sellAndBuyMutex));
		SellAndBuyContainer container;
		sell_buy_containers.insert(std::make_pair(stockID, container));
	}
}

// 将订单加入至待售卖容器
void TradingMarket::addOrderToSell(const std::string& stockID, const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	sell_buy_containers.at(stockID).sell.emplace(orderID);
}

// 将订单加入至待购买容器
void TradingMarket::addOrderToBuy(const std::string& stockID, const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	sell_buy_containers.at(stockID).buy.emplace(orderID);
}

// 将订单从售卖容器中删除
void TradingMarket::delOrderFromSell(const std::string& stockID, const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	if(sell_buy_containers.at(stockID).sell.find(orderID)!=sell_buy_containers.at(stockID).sell.end()){
		sell_buy_containers.at(stockID).sell.erase(orderID);
	}
}

// 将订单从购买容器中删除
void TradingMarket::delOrderFromBuy(const std::string& stockID, const uint64_t& orderID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	if(sell_buy_containers.at(stockID).buy.find(orderID)!=sell_buy_containers.at(stockID).buy.end()){
		sell_buy_containers.at(stockID).buy.erase(orderID);
	}
}

// 判断该股票订单是否在容器中
bool TradingMarket::existInContainers(const std::string& stockID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	if(sell_buy_containers.find(stockID)!=sell_buy_containers.end()){
		return true;
	}
	return false;
}

// 获取卖订单集合的引用
std::set<uint64_t>& TradingMarket::getSellOrderSet(const std::string& stockID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	return sell_buy_containers.at(stockID).sell;
}

// 获取买订单集合的引用
std::set<uint64_t>& TradingMarket::getBuyOrderSet(const std::string& stockID){
	// 读锁
	std::shared_lock<std::shared_mutex> r(rw_stocks_mutex);
	return sell_buy_containers.at(stockID).buy;
}
#endif
