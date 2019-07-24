#ifndef _IRPCSERVER_H_
#define _IRPCSERVER_H_

#ifdef WIN32
#ifdef RPCSERVER_EXPORTS
#define RPCSERVER_DLL_EXPORTS __declspec(dllexport)
#else
#define RPCSERVER_DLL_EXPORTS __declspec(dllimport)
#endif
#else
#define RPCSERVER_DLL_EXPORTS
#endif

#include <string>
#include <google/protobuf/service.h>

//RpcServer接口
class RPCSERVER_DLL_EXPORTS IRpcServer
{
public:
	virtual ~IRpcServer();

	/************************************************************************
	功  能：创建RpcServer实例
	参  数：
		ip：输入，服务器监听的ip地址
		port：输入，服务器监听的端口号
		IOWorkerNum：输入，IOWorker池Worker数量
		IOWorkerAcceptQueueMaxSize：输入，IOWorker接管连接队列的最大长度
		IOWorkerWriteQueueMaxSize：输入，IOWorker发送队列的最大长度
		businessWorkerNum：输入，业务Worker池Worker数量
		businessWorkerQueueMaxSize：输入，业务Worker业务任务队列的最大长度
	返回值：IRpcServer指针
	************************************************************************/
	static IRpcServer *createRpcServer(const std::string &ip, int port, 
		unsigned int IOWorkerNum, unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize, 
		unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize);

	/************************************************************************
	功  能：销毁RpcServer实例
	参  数：
		pIRpcServer：IRpcServer指针
	返回值：无
	************************************************************************/
	static void releaseRpcServer(IRpcServer *pIRpcServer);

	/************************************************************************
	功  能：注册服务
	参  数：
		pService：服务指针
	返回值：无
	************************************************************************/
	virtual void registerService(google::protobuf::Service *pService) = 0;

	/************************************************************************
	功  能：启动RpcServer
	参  数：无
	返回值：无
	************************************************************************/
	virtual void start() = 0;

	/************************************************************************
	功  能：结束RpcServer
	参  数：无
	返回值：无
	************************************************************************/
	virtual void end() = 0;
};

#endif