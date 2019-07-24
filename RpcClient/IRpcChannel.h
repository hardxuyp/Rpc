#ifndef _IRPCCHANNEL_H_
#define _IRPCCHANNEL_H_

#include <string>
#include <google/protobuf/service.h>
#include "IRpcClient.h"

#ifdef WIN32
#ifdef RPCCLIENT_EXPORTS
#define RPCCLIENT_DLL_EXPORTS __declspec(dllexport)
#else
#define RPCCLIENT_DLL_EXPORTS __declspec(dllimport)
#endif
#else
#define RPCCLIENT_DLL_EXPORTS
#endif

//RpcChannel接口
class RPCCLIENT_DLL_EXPORTS IRpcChannel : public google::protobuf::RpcChannel
{
public:
	virtual ~IRpcChannel();

	/************************************************************************
	功  能：创建RpcChannel实例
	参  数：
		pIClient：输入，IRpcClient指针
		ip：输入，服务器监听的ip地址
		port：输入，服务器监听的端口号
	返回值：IRpcChannel指针
	************************************************************************/
	static IRpcChannel *createRpcChannel(IRpcClient *pIClient, const std::string &ip, int port);

	/************************************************************************
	功  能：销毁RpcChannel实例
	参  数：
		pIRpcChannel：pIRpcChannel指针
	返回值：无
	************************************************************************/
	static void releaseRpcChannel(IRpcChannel *pIRpcChannel);

	/************************************************************************
	功  能：调用方法
	参  数：见google::protobuf::RpcChannel CallMethod()描述
	返回值：无
	************************************************************************/
	virtual void CallMethod(const google::protobuf::MethodDescriptor* method,
		google::protobuf::RpcController* controller,
		const google::protobuf::Message* request,
		google::protobuf::Message* response,
		google::protobuf::Closure* done) = 0;
};

#endif