#ifndef _IRPCCLIENT_H_
#define _IRPCCLIENT_H_

#ifdef WIN32
#ifdef RPCCLIENT_EXPORTS
#define RPCCLIENT_DLL_EXPORTS __declspec(dllexport)
#else
#define RPCCLIENT_DLL_EXPORTS __declspec(dllimport)
#endif
#else
#define RPCCLIENT_DLL_EXPORTS
#endif

#ifdef WIN32
#include <winsock2.h>
#endif

//RpcClient接口
class RPCCLIENT_DLL_EXPORTS IRpcClient
{
public:
	virtual ~IRpcClient();

	/************************************************************************
	功  能：创建RpcClient实例
	参  数：
		IOWorkerNum：输入，IOWorker池Worker数量
		IOWorkerQueueMaxSize：输入，IOWorker队列的最大长度
		heartbeatInterval：输入，连接上PING心跳的发送周期
	返回值：IRpcClient指针
	************************************************************************/
	static IRpcClient *createRpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval);

	/************************************************************************
	功  能：销毁RpcClient实例
	参  数：
		pIRpcClient：IRpcClient指针
	返回值：无
	************************************************************************/
	static void releaseRpcClient(IRpcClient *pIRpcClient);

	/************************************************************************
	功  能：启动RpcClient
	参  数：无
	返回值：无
	************************************************************************/
	virtual void start() = 0;

	/************************************************************************
	功  能：结束RpcClient
	参  数：无
	返回值：无
	************************************************************************/
	virtual void end() = 0;
};

#endif