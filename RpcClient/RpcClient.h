#ifndef _RPCCLIENT_H_
#define _RPCCLIENT_H_

#include "IRpcClient.h"
#include "IOWorker.h"

//客户端
class RpcClient : public IRpcClient
{
public:
	/************************************************************************
	功  能：构造方法
	参  数：
		IOWorkerNum：输入，IOWorker池Worker数量
		IOWorkerQueueMaxSize：输入，IOWorker队列的最大长度
		heartbeatInterval：输入，连接上PING心跳的发送周期
	返回值：无
	************************************************************************/
	RpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval);

	/************************************************************************
	功  能：析构方法
	参  数：无
	返回值：无
	************************************************************************/
	~RpcClient();

	/************************************************************************
	功  能：启动RpcClient
	参  数：无
	返回值：无
	************************************************************************/
	virtual void start();

	/************************************************************************
	功  能：结束RpcClient
	参  数：无
	返回值：无
	************************************************************************/
	virtual void end();

	/************************************************************************
	功  能：从IOWorker池中调度一个IOWorker并在该IOWorker上分配连接
	参  数：
		pConn：输出，分配的连接
	返回值：选中的IOWorker指针，若为NULL，表示调度失败
	************************************************************************/
	IOWorker *schedule(Conn *&pConn);

private:
	//IOWorker池：map<IOWorker对应的socketpair的写端描述符, IOWorker指针>
	map<evutil_socket_t, IOWorker *> m_mapWorker;
	//RpcServer是否已经开始启动
	bool m_bStarted;
	//RpcServer是否已经开始结束
	bool m_bEnded;
};

#endif