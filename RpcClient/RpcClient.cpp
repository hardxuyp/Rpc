#include "RpcClient.h"

IRpcClient::~IRpcClient() {}

IRpcClient *IRpcClient::createRpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval)
{
	return new RpcClient(IOWorkerNum, IOWorkerQueueMaxSize, heartbeatInterval);
}

void IRpcClient::releaseRpcClient(IRpcClient *pIRpcClient)
{
	SAFE_DELETE(pIRpcClient)
}

RpcClient::RpcClient(unsigned int IOWorkerNum, unsigned int IOWorkerQueueMaxSize, timeval heartbeatInterval)
{
	m_bStarted = false;
	m_bEnded = false;

#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	//创建IOWorker池
	for (unsigned int i = 0; i < IOWorkerNum; ++i)
	{
		evutil_socket_t fds[2];
		if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) < 0)
			continue;
		//将socketpair的2个描述符都设置成非阻塞
		evutil_make_socket_nonblocking(fds[0]);
		evutil_make_socket_nonblocking(fds[1]);
		m_mapWorker[fds[1]] = new IOWorker(fds, IOWorkerQueueMaxSize, heartbeatInterval);
	}
}

RpcClient::~RpcClient()
{
	end();
}

void RpcClient::start()
{
	//不允许RpcClient重复启动
	if (m_bStarted)
		return;
	m_bStarted = true;
	//启动IOWorker池
	for (auto it = m_mapWorker.begin(); it != m_mapWorker.end(); ++it)
	{
		IOWorker *pWorker = it->second;
		if (NULL == pWorker)
			continue;
		pWorker->start();
	}
}

void RpcClient::end()
{
	//不允许RpcClient重复结束
	if (m_bEnded)
		return;
	m_bEnded = true;

	//销毁IOWorker池
	for (auto it = m_mapWorker.begin(); it != m_mapWorker.end(); ++it)
	{
		if (NULL != it->second)
			SAFE_DELETE(it->second)
		//关闭socketpair的写端描述符
		evutil_closesocket(it->first);
	}
}

IOWorker *RpcClient::schedule(Conn *&pConn)
{
	IOWorker *pSelectedWorker = NULL;
	unsigned int minBusyLevel;

	auto it = m_mapWorker.begin();
	for ( ; it != m_mapWorker.end(); ++it)
	{
		IOWorker *pWorker = it->second;
		if (NULL != pWorker)
		{
			pSelectedWorker = pWorker;
			//获取IOWorker的繁忙程度，越不忙的IOWorker越容易被选中
			minBusyLevel = pSelectedWorker->getBusyLevel();
			break;
		}
	}
	if (NULL == pSelectedWorker)
		return NULL;

	if (minBusyLevel > 0)
	{
		for (++it; it != m_mapWorker.end(); ++it)
		{
			IOWorker *pWorker = it->second;
			if (NULL != pWorker)
			{
				unsigned int busyLevel = pWorker->getBusyLevel();
				if (busyLevel < minBusyLevel)
				{
					pSelectedWorker = pWorker;
					minBusyLevel = busyLevel;
					//若IOWorker完全空闲，则立即挑选该IOWorker
					if (minBusyLevel <= 0)
						break;
				}
			}
		}
	}
	//分配连接
	pConn = pSelectedWorker->genConn();
	return (NULL != pConn) ? pSelectedWorker : NULL;
}