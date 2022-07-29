#ifndef HELPER_H
#define HELPER_H

#include <string>
#include <iostream>
#include <time.h>
#include "../proto/OrderProcessSystem.grpc.pb.h"

using OPS::NewOrderRequest;
using OPS::CancelOrderRequest;
using OPS::ExecutionReport;
using OPS::OrderReport;
using OPS::OrderService;

void printRequest(const NewOrderRequest&);
void printRequest(const CancelOrderRequest&);
void printReport(const ExecutionReport&);
void printReport(const OrderReport&);
std::string getTime();
bool checkRequest(const NewOrderRequest&, std::string&);
void initReport(ExecutionReport&, const NewOrderRequest&);
void initReport(ExecutionReport&, const CancelOrderRequest&);
void initReport(OrderReport&, const NewOrderRequest&, const uint64_t&);
#endif 
