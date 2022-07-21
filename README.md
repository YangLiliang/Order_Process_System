# Order_Process_System
## Order_Process_System
The initial framework of OPS supports synchronous grpc. the system supports multi-user concurrency, but the lock granularity is large.
## make
```
cd OrderProcessSystem_v_2
make
```
## run server
```
./OPSServer
```
## run client
```
./OPSClient <N/C> <Request Filename/OrderID>
#example:
./OPSClient N request1
./OPSClient C 1
```
N代表NewOrderRequest

C代表CancelOrderRequest

## clean
```
make clean
```
