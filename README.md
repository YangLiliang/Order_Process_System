# Order_Process_System
## Order_Process_System
1. The initial framework of OPS supports sync grpc. 

2. The system supports multi-user concurrency, but the lock granularity is large.
## Order_Process_System_v_2
1. Supports synchronous grpc. 

2. lock granularity is reduced and concurrency is increased. 

3. Server supports real-time transaction information sent to both sides of the transaction. 

4. The transaction-related api is encapsulated into a Singleton mode class that provides interfaces for the server and client.

5. Client-server exit is not graceful.
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
