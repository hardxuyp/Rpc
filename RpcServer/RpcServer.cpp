#include "RpcServer.h"

IRpcServer::~IRpcServer() {}

IRpcServer *IRpcServer::createRpcServer(const string &ip, int port, 
	unsigned int IOWorkerNum, unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize, 
	unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize)
{
	return new RpcServer(ip, port, IOWorkerNum, IOWorkerAcceptQueueMaxSize, IOWorkerCompleteQueueMaxSize, businessWorkerNum, businessWorkerQueueMaxSize);
}

void IRpcServer::releaseRpcServer(IRpcServer *pIRpcServer)
{
	SAFE_DELETE(pIRpcServer);
}

RpcServer::RpcServer(const string &ip, int port, 
	unsigned int IOWorkerNum, unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize, 
	unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize)
{
	m_ip = ip;
	m_port = port;
	m_pEvBase = NULL;
	m_bStarted = false;
	m_bEnded = false;

#ifdef WIN32
	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

	//创建socketpair以让外部通知server结束
	evutil_socket_t notif_fds[2];
	evutil_socketpair(AF_INET, SOCK_STREAM, 0, notif_fds);
	m_notified_fd = notif_fds[0];
	m_notify_fd = notif_fds[1];

	//创建业务Worker池
	for (unsigned int i = 0; i < businessWorkerNum; ++i)
		m_vecBusinessWorker.push_back(new BusinessWorker(businessWorkerQueueMaxSize));

	//创建IOWorker池
	for (unsigned int i = 0; i < IOWorkerNum; ++i)
	{
		evutil_socket_t fds[2];
		if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) < 0)
			continue;
		//将socketpair的2个描述符都设置成非阻塞
		evutil_make_socket_nonblocking(fds[0]);
		evutil_make_socket_nonblocking(fds[1]);
		m_mapIOWorker[fds[1]] = new IOWorker(fds, IOWorkerAcceptQueueMaxSize, IOWorkerCompleteQueueMaxSize, &m_vecBusinessWorker);
	}
}

RpcServer::~RpcServer()
{
	end();
}

void RpcServer::registerService(google::protobuf::Service *pService)
{
	if (NULL == pService)
		return;
	//获取服务描述指针
	const google::protobuf::ServiceDescriptor *pServiceDescriptor = pService->GetDescriptor();
	if (NULL == pServiceDescriptor)
		return;

	pair<google::protobuf::Service *, vector<const google::protobuf::MethodDescriptor *> > &v = m_mapRegisteredService[pServiceDescriptor->full_name()];
	//存放服务指针
	v.first = pService;
	//存放服务对应的方法描述指针
	for (int i = 0; i < pServiceDescriptor->method_count(); ++i)
	{
		const google::protobuf::MethodDescriptor *pMethodDescriptor = pServiceDescriptor->method(i);
		if (NULL == pMethodDescriptor)
			continue;
		v.second.emplace_back(pMethodDescriptor);
	}
}

void RpcServer::start()
{
	//不允许RpcServer重复启动
	if (m_bStarted)
		return;
	m_bStarted = true;

	//创建event_base
	event_base *pEvBase = event_base_new();
	if (NULL == pEvBase)
		return;
	//将创建的event_base和智能指针绑定，以让创建的event_base自动销毁
	unique_ptr<event_base, function<void(event_base *)> > ptrEvBase(pEvBase, event_base_free);

	//创建服务器监听地址
	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	if (0 == evutil_inet_pton(AF_INET, m_ip.c_str(), &(serverAddr.sin_addr)))
		return;
	serverAddr.sin_port = htons(m_port);
	//让libevent listen并accept，accept后将回调acceptCallback()函数
	evconnlistener *pListener = evconnlistener_new_bind(pEvBase, acceptCallback, this, LEV_OPT_CLOSE_ON_FREE, 128, (sockaddr *)(&serverAddr), sizeof(serverAddr));
	if (NULL == pListener)
		return;
	//将创建的evconnlistener和智能指针绑定，以让创建的evconnlistener自动销毁
	unique_ptr<evconnlistener, function<void(evconnlistener *)> > ptrListener(pListener, evconnlistener_free);

	//启动IOWorker池
	for (auto it = m_mapIOWorker.begin(); it != m_mapIOWorker.end(); ++it)
	{
		if (NULL != it->second)
			it->second->start();
	}

	//启动业务Worker池
	for (auto it = m_vecBusinessWorker.begin(); it != m_vecBusinessWorker.end(); ++it)
	{
		if (NULL != *it)
		{
			//将注册的所有服务告知业务Worker
			(*it)->setRegisteredServices(&m_mapRegisteredService);
			(*it)->start();
		}
	}

	//创建socketpair的读端描述符上的可读事件
	event *pNotifiedEv = event_new(pEvBase, m_notified_fd, EV_READ | EV_PERSIST, serverNotifiedCallback, this);
	if (NULL == pNotifiedEv)
		return;
	//将创建的事件和智能指针绑定，以让创建的事件自动销毁
	unique_ptr<event, function<void(event *)> > ptrNotifiedEv(pNotifiedEv, event_free);
	//将socketpair的读端描述符上的可读事件设置为未决的
	event_add(pNotifiedEv, NULL);

	m_pEvBase = pEvBase;
	//启动事件循环
	event_base_dispatch(pEvBase);
}

void RpcServer::end()
{
	//不允许RpcServer重复结束
	if (m_bEnded)
		return;
	m_bEnded = true;

	//销毁IOWorker池
	for (auto it = m_mapIOWorker.begin(); it != m_mapIOWorker.end(); ++it)
	{
		if (NULL != it->second)
			SAFE_DELETE(it->second)
			//关闭socketpair的写端描述符
			evutil_closesocket(it->first);
	}

	//销毁业务Worker池
	for (auto it = m_vecBusinessWorker.begin(); it != m_vecBusinessWorker.end(); ++it)
	{
		if (NULL != *it)
			SAFE_DELETE(*it)
	}

	//通知RpcServer结束事件循环
	char buf[1] = { NOTIFY_SERVER_END };
	send(m_notify_fd, buf, 1, 0);
}

void serverNotifiedCallback(evutil_socket_t fd, short events, void *pArg)
{
	if (NULL == pArg)
		return;

	//通过socketpair的读端描述符读取通知类型
	char buf[1];
	recv(fd, buf, 1, 0);

	if (NOTIFY_SERVER_END == buf[0])
		((RpcServer *)pArg)->handleEnd();
}

void RpcServer::handleEnd()
{
	//关闭socketpair的读、写端描述符
	evutil_closesocket(m_notified_fd);
	evutil_closesocket(m_notify_fd);

	//退出事件循环
	if (NULL != m_pEvBase)
		event_base_loopexit(m_pEvBase, NULL);
}

void acceptCallback(evconnlistener *pListener, evutil_socket_t fd, struct sockaddr *pAddr, int socklen, void *pArg)
{
	if (NULL != pArg)
		((RpcServer *)pArg)->handleAccept(fd);
}

void RpcServer::handleAccept(evutil_socket_t fd)
{
	//将libevent accept的连接描述符设置成非阻塞
	evutil_make_socket_nonblocking(fd);

	//调度IOWorker接管连接
	IOWorker *pSelectedWorker = schedule();
	if (NULL != pSelectedWorker)
	{
		//获取socketpair的写端描述符
		evutil_socket_t selectedNotify_fd = pSelectedWorker->getNotify_fd();
		//将连接描述符放入选中IOWorker的accept队列
		pSelectedWorker->m_acceptQueue.put(fd);
		//通知选中的IOWorker
		char buf[1] = { NOTIFY_IOWORKER_ACCEPT };
		send(selectedNotify_fd, buf, 1, 0);
	}
}

IOWorker *RpcServer::schedule()
{
	IOWorker *pSelectedWorker = NULL;
	unsigned int minBusyLevel;

	auto it = m_mapIOWorker.begin();
	for ( ; it != m_mapIOWorker.end(); ++it)
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
	if (NULL == pSelectedWorker || minBusyLevel <= 0)
		return pSelectedWorker;

	for (++it; it != m_mapIOWorker.end(); ++it)
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
	return pSelectedWorker;
}