#include <boost/thread/thread.hpp>
#include "Test.pb.h"
#include "IRpcServer.h"
#include "RpcController.h"

//服务实现类
class NumServiceImpl : public testNamespace::NumService
{
public:
	virtual void add(::google::protobuf::RpcController* controller,
		const ::testNamespace::NumRequest* request,
		::testNamespace::NumResponse* response,
		::google::protobuf::Closure* done)
	{
		response->set_output(request->input1() + request->input2());
	}

	virtual void minus(::google::protobuf::RpcController* controller,
		const ::testNamespace::NumRequest* request,
		::testNamespace::NumResponse* response,
		::google::protobuf::Closure* done)
	{
		response->set_output(request->input1() - request->input2());
	}
};

//销毁RpcServer实例测试，实际开发中不要这么做，因为server理论上永不主动停止
void releaseServer(IRpcServer *pIServer)
{
	//线程休眠几秒，让server彻底启动起来
	boost::this_thread::sleep_for(boost::chrono::seconds(3));
	//销毁RpcServer实例
	IRpcServer::releaseRpcServer(pIServer);
}

int main()
{
	//创建RpcServer实例
	IRpcServer *pIServer = IRpcServer::createRpcServer("127.0.0.1", 8888, 3, 50, 50, 3, 50);
	//创建服务实现类实例
	NumServiceImpl numService;
	//注册服务
	pIServer->registerService(&numService);

	//销毁RpcServer实例测试，实际开发中不要这么做，因为server理论上永不主动停止
	//boost::thread thd(releaseServer, pIServer);

	//启动RpcServer
	pIServer->start();

	//等待线程退出
	//thd.join();

	return 0;
}