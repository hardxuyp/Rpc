#include <iostream>
#include <google/protobuf/stubs/common.h>
#include "RpcController.h"
#include "Test.pb.h"
#include "IRpcClient.h"
#include "IRpcChannel.h"

//异步调用的回调函数
void callback(testNamespace::NumResponse *pResp, google::protobuf::RpcController *pController)
{
	if (NULL == pResp)
		return;

	if (NULL != pController)
	{
		if (pController->Failed())
			return;
	}

	std::cout << "async call result: 3 + 4 = " << pResp->output() << std::endl;
}

int main()
{
	//创建RpcClient实例
	timeval t = {3, 0};
	IRpcClient *pIClient = IRpcClient::createRpcClient(3, 50, t);
	pIClient->start();
	//创建RpcChannel实例，一个channel代表一个连接
	IRpcChannel *pIChannel = IRpcChannel::createRpcChannel(pIClient, "127.0.0.1", 8888);
	//创建服务的客户端stub实例
	testNamespace::NumService::Stub numServiceStub((google::protobuf::RpcChannel *)pIChannel);
	//创建request，并赋值
	testNamespace::NumRequest req;
	req.set_input1(3);
	req.set_input2(4);
	//创建response
	testNamespace::NumResponse resp;
	//创建RpcController实例，用来表示成功与否以及失败原因
	RpcController controller;

	//done参数传入NULL以进行同步调用
	numServiceStub.minus(&controller, &req, &resp, NULL);
	if (controller.Failed())
		std::cout << "sync call error: " << controller.ErrorText() << std::endl;
	else
		std::cout << "sync call result: 3 - 4 = " << resp.output() << std::endl;

	//done参数传入回调信息以进行异步调用
	controller.Reset();
	numServiceStub.add(&controller, &req, &resp, google::protobuf::NewCallback(callback, &resp, (google::protobuf::RpcController *)(&controller)));
	
	//销毁RpcChannel实例
	//IRpcChannel::releaseRpcChannel(pIChannel);
	//销毁RpcClient实例
	//IRpcClient::releaseRpcClient(pIClient);

	while (true) {}
	
	return 0;
}