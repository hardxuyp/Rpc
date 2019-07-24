#include "IOWorker.h"

IOWorker::IOWorker(evutil_socket_t *fds, unsigned int queueMaxSize, timeval heartbeatInterval)
{
	m_notified_fd = fds[0];
	m_notify_fd = fds[1];
	m_pEvBase = NULL;
	m_heartbeatInterval = heartbeatInterval;
	m_bStarted = false;
	m_bEnded = false;
	m_queue.setMaxSize(queueMaxSize);
}

IOWorker::~IOWorker()
{
	//通知IOWorker结束事件循环
	char buf[1] = { NOTIFY_IOWORKER_END };
	send(m_notify_fd, buf, 1, 0);
	//等待线程结束
	m_thd.join();
	//关闭socketpair的读端描述符
	evutil_closesocket(m_notified_fd);
}

void IOWorker::start()
{
	//不允许IOWorker重复启动
	if (m_bStarted)
		return;
	m_bStarted = true;

	//启动线程
	m_thd = std::move(boost::thread(&IOWorker::threadMain, this, m_notified_fd));
}

void IOWorker::threadMain(evutil_socket_t notified_fd)
{
	//创建event_base
	event_base *pEvBase = event_base_new();
	if (NULL == pEvBase)
	{
		evutil_closesocket(notified_fd);
		return;
	}
	//将创建的event_base和智能指针绑定，以让创建的event_base自动销毁并释放资源
	unique_ptr<event_base, function<void(event_base *)> > ptrEvBase(pEvBase, [&notified_fd](event_base *p) {
		event_base_free(p);
		//关闭socketpair的读端描述符
		evutil_closesocket(notified_fd);
	});

	//创建socketpair的读端描述符上的可读事件
	event *pNotifiedEv = event_new(pEvBase, notified_fd, EV_READ | EV_PERSIST, notifiedCallback, this);
	if (NULL == pNotifiedEv)
		return;
	//将创建的事件和智能指针绑定，以让创建的事件自动销毁
	unique_ptr<event, function<void(event *)> > ptrNotifiedEv(pNotifiedEv, event_free);

	//将socketpair的读端描述符上的可读事件设置为未决的
	if (0 != event_add(pNotifiedEv, NULL))
		return;

	m_pEvBase = pEvBase;
	//启动事件循环
	event_base_dispatch(pEvBase);
}

void notifiedCallback(evutil_socket_t fd, short events, void *pArg)
{
	if (NULL == pArg)
		return;

	//通过socketpair的读端描述符读取通知类型
	char buf[1];
	recv(fd, buf, 1, 0);
	IOWorker *pWorker = (IOWorker *)pArg;

	if (NOTIFY_IOWORKER_IOTASK == buf[0])
	{
		pWorker->handleIOTask();
	}
	else if (NOTIFY_IOWORKER_END == buf[0])
		pWorker->handleEnd();
}

void IOWorker::handleIOTask()
{
	//为避免过多的锁操作，一次性从队列中取出所有IO任务
	list<IOTask> queue;
	m_queue.takeAll(queue);
	for (auto it = queue.begin(); it != queue.end(); ++it)
	{
		if (IOTask::CONNECT == (*it).type)
		{
			connect((Conn *)(*it).pData);
		}
		else if (IOTask::DISCONNECT == (*it).type)
		{
			unsigned int *pConnId = (unsigned int *)(*it).pData;
			if (NULL != pConnId)
			{
				handleDisconnect(*pConnId);
				delete pConnId;
			}
		}
		else if (IOTask::CALL == (*it).type)
			handleCall((Call *)(*it).pData);
	}
}

void IOWorker::handleDisconnect(unsigned int connId)
{
	//从m_mapConn中找连接
	auto it = m_mapConn.find(connId);
	if (it == m_mapConn.end())
		return;

	Conn *pConn = it->second;
	if (NULL != pConn)
	{
		//释放连接
		freeConn(pConn);
		delete pConn;
	}
	m_mapConn.erase(it);
}

void IOWorker::handleCall(Call *pCall)
{
	if (NULL == pCall)
		return;

	//从m_mapConn中找连接
	auto it = m_mapConn.find(pCall->connId);
	if (it == m_mapConn.end())
	{
		m_connIdGen.back(pCall->connId);
		SAFE_DELETE(pCall)
		return;
	}
	Conn *pConn = it->second;
	if (NULL == pConn)
	{
		m_connIdGen.back(pCall->connId);
		m_mapConn.erase(it);
		SAFE_DELETE(pCall)
		return;
	}
	if (!pConn->bConnected)
	{
		//若连接尚未建立或连接已断开，重新将调用指针放回IO任务队列
		IOTask task;
		task.type = IOTask::CALL;
		task.pData = pCall;
		m_queue.put(task);
		return;
	}
	if (NULL == pConn->pBufEv)
	{
		SAFE_DELETE(pCall)
		return;
	}
	evbuffer *pOutBuf = bufferevent_get_output(pConn->pBufEv);
	if (NULL == pOutBuf)
	{
		SAFE_DELETE(pCall)
		return;
	}
	callId_t callId;
	if (!pConn->idGen.generate(callId))
	{
		//设置错误信息：调用id资源不足，然后返回用户调用
		pCall->pController->SetFailed("call id not enough");
		rpcCallback(pCall->pClosure, pCall->sync_write_fd);
		SAFE_DELETE(pCall)
		return;
	}
	
	//创建请求的协议body
	ProtocolBodyRequest bodyReq;
	bodyReq.set_callid(callId);
	bodyReq.set_servicename(pCall->serviceName);
	bodyReq.set_methodindex(pCall->methodIndex);
	bodyReq.set_content(*pCall->pStrReq);
	//序列化请求的协议body
	string strBuf;
	if (!bodyReq.SerializeToString(&strBuf))
	{
		//设置错误信息：请求序列化失败，然后返回用户调用
		pCall->pController->SetFailed("request serialized failed");
		rpcCallback(pCall->pClosure, pCall->sync_write_fd);
		SAFE_DELETE(pCall)
		return;
	}
	bodySize_t bodyLen = strBuf.size();
	unsigned char arr[HEAD_SIZE] = {0};
	arr[0] = DATA_TYPE_REQUEST;
	//以下这句代码要注意字节序，防止通信对端解析错误，暂未考虑字节序
	memcpy(arr + 1, &bodyLen, HEAD_SIZE - 1);

	//将请求的协议head放入evbuffer
	evbuffer_add(pOutBuf, arr, HEAD_SIZE);
	//将序列化后的请求的协议body放入evbuffer
	evbuffer_add(pOutBuf, strBuf.c_str(), bodyLen);

	SAFE_DELETE(pCall->pStrReq)
	pConn->mapCall[callId] = pCall;
}

void IOWorker::handleEnd()
{
	//不允许IOWorker重复结束
	if (m_bEnded)
		return;
	m_bEnded = true;

	//退出事件循环
	if (NULL != m_pEvBase)
		event_base_loopexit(m_pEvBase, NULL);

	//释放连接
	for (auto it = m_mapConn.begin(); it != m_mapConn.end(); ++it)
	{
		unsigned int connId = it->first;
		m_connIdGen.back(connId);
		freeConn(it->second);
		SAFE_DELETE(it->second)
	}
	m_mapConn.clear();
}

void readCallback(bufferevent *pBufEv, void *pArg)
{
	if (NULL == pArg)
		return;
	Conn *pConn = (Conn *)pArg;
	IOWorker *pWorker = pConn->pWorker;
	if (NULL != pWorker)
		pWorker->handleRead(pConn);
}

void IOWorker::handleRead(Conn *pConn)
{
	if (NULL == pConn)
		return;
	bufferevent *pBufEv = pConn->pBufEv;
	if (NULL == pBufEv)
		return;
	//获取bufferevent中的输入evbuffer指针
	evbuffer *pInBuf = bufferevent_get_input(pBufEv);
	if (NULL == pInBuf)
		return;

	//此连接上收到了数据，则主观认为连接仍然保持着
	pConn->bConnectionMightLost = false;

	//由于TCP协议是一个字节流的协议，所以必须依靠应用层协议的规格来区分协议数据的边界
	while (true)
	{
		if (PROTOCOL_HEAD == pConn->inState)
		{
			//获取输入evbuffer长度
			if (evbuffer_get_length(pInBuf) < HEAD_SIZE)
				break;
			//由于evbuffer中的数据可能分散在不连续的内存块，所以若需要获取字节数组，必须调用evbuffer_pullup()进行“线性化”
			//获取协议head字节数组
			unsigned char *pArr = evbuffer_pullup(pInBuf, HEAD_SIZE);
			if (NULL == pArr)
				break;
			//将协议head第2-5字节转成协议body长度
			pConn->inBodySize = *((bodySize_t *)(pArr + 1));
			//通过协议head第1字节判断数据类型
			//若数据类型为PONG心跳，则从输入evbuffer中移出协议数据
			if (DATA_TYPE_HEARTBEAT_PONG == pArr[0])
				evbuffer_drain(pInBuf, HEAD_SIZE + pConn->inBodySize);
			//若数据类型为其它类型（一般是请求），则继续读取协议body
			else
			{
				evbuffer_drain(pInBuf, HEAD_SIZE);
				pConn->inState = PROTOCOL_BODY;
			}
		}
		else if (PROTOCOL_BODY == pConn->inState)
		{
			//获取输入evbuffer长度
			if (evbuffer_get_length(pInBuf) < pConn->inBodySize)
				break;

			unsigned char *pArr = evbuffer_pullup(pInBuf, HEAD_SIZE);
			if (NULL != pArr)
			{
				//反序列化响应的协议body
				ProtocolBodyResponse bodyResp;
				if (!bodyResp.ParseFromArray(pArr, pConn->inBodySize))
				{
					evbuffer_drain(pInBuf, pConn->inBodySize);
					pConn->inState = PROTOCOL_HEAD;
					continue;
				}
				//通过调用id找到调用指针
				auto it = pConn->mapCall.find(bodyResp.callid());
				if (it != pConn->mapCall.end())
				{
					Call *pCall = it->second;
					if (NULL != pCall)
					{
						google::protobuf::Message *pRespMessage = pCall->pRespMessage;
						if (NULL != pRespMessage)
						{
							//反序列化响应并返回用户调用
							if (pRespMessage->ParseFromString(bodyResp.content()))
								rpcCallback(pCall->pClosure, pCall->sync_write_fd);
						}
						pConn->idGen.back(pCall->callId);
						pConn->mapCall.erase(it);
						SAFE_DELETE(pCall)
					}
				}
			}
			evbuffer_drain(pInBuf, pConn->inBodySize);
			pConn->inState = PROTOCOL_HEAD;
		}
		else
			break;
	}
}

void eventCallback(bufferevent *pBufEv, short events, void *pArg)
{
	//events：1、连接上；2、eof；3、超时；4、各种错误
	auto del = [](Conn *pConn) {
		if (NULL == pConn)
			return;
		if (NULL != pConn->pBufEv)
		{
			bufferevent_free(pConn->pBufEv);
			pConn->pBufEv = NULL;
		}
		for (auto it = pConn->mapCall.begin(); it != pConn->mapCall.end(); ++it)
			SAFE_DELETE(it->second)
		delete pConn;
	};

	if (NULL == pArg)
		return;
	Conn *pConn = (Conn *)pArg;
	IOWorker *pWorker = pConn->pWorker;
	if (NULL == pWorker)
	{
		del(pConn);
		return;
	}
	if (0 != (events & BEV_EVENT_CONNECTED))
	{
		pWorker->handleConnected(pConn);
		return;
	}
	if (0 != (events & BEV_EVENT_TIMEOUT))
	{
		pWorker->handleTimeout(pConn);
		return;
	}
	pWorker->connect(pConn);
}

void IOWorker::handleConnected(Conn *pConn)
{
	if (NULL == pConn)
		return;

	pConn->bConnected = true;
	//在bufferevent上设置超时时间，用来定时发送PING心跳
	if (NULL != pConn->pBufEv && m_heartbeatInterval.tv_sec >= 0)
		bufferevent_set_timeouts(pConn->pBufEv, &m_heartbeatInterval, NULL);

	//处理之前可能未处理的IO任务
	handleIOTask();
}

void IOWorker::handleTimeout(Conn *pConn)
{
	if (NULL == pConn)
		return;
	bufferevent *pBufEv = pConn->pBufEv;
	if (NULL == pBufEv)
		return;
	
	if (pConn->bConnected && !pConn->bConnectionMightLost)
	{
		//获取bufferevent中的输出evbuffer指针
		evbuffer *pOutBuf = bufferevent_get_output(pBufEv);
		if (NULL != pOutBuf)
		{
			//创建PING心跳的协议head，协议body长度为0
			unsigned char arr[HEAD_SIZE] = {0};
			arr[0] = DATA_TYPE_HEARTBEAT_PING;
			//将协议数据放进输出evbuffer
			evbuffer_add(pOutBuf, arr, sizeof(arr));

			cout << "IO thread " << boost::this_thread::get_id() << " finishes sending PING heartbeat." << endl;
		}
		//暂时主观认为连接已丢失，等下次收到PONG心跳回复或调用的响应数据，再置false
		pConn->bConnectionMightLost = true;
	}
	else
		//连接客观或主观已丢失，重新连接
		connect(pConn);

	//重新使能bufferevent上的可读事件
	bufferevent_enable(pConn->pBufEv, EV_READ);
}

bool IOWorker::connect(Conn *pConn)
{
	if (NULL == pConn)
		return false;

	//释放连接
	freeConn(pConn);

	//创建bufferevent
	bufferevent *pBufEv = bufferevent_socket_new(m_pEvBase, -1, BEV_OPT_CLOSE_ON_FREE);
	if (NULL == pBufEv)
		return false;
	pConn->pBufEv = pBufEv;
	bufferevent_setcb(pBufEv, readCallback, NULL, eventCallback, pConn);
	bufferevent_enable(pBufEv, EV_READ);

	if (bufferevent_socket_connect(pBufEv, (sockaddr *)(&pConn->serverAddr), sizeof(pConn->serverAddr)) < 0)
		return false;

	//获取连接描述符
	pConn->fd = bufferevent_getfd(pBufEv);
	//将连接描述符设置成非阻塞
	evutil_make_socket_nonblocking(pConn->fd);

	return true;
}

void IOWorker::freeConn(Conn *pConn)
{
	if (NULL == pConn)
		return;
	pConn->bConnected = false;
	//释放bufferevent
	if (NULL != pConn->pBufEv)
	{
		bufferevent_free(pConn->pBufEv);
		pConn->pBufEv = NULL;
	}
	//销毁连接上的所有调用
	for (auto it = pConn->mapCall.begin(); it != pConn->mapCall.end(); ++it)
	{
		callId_t callId = it->first;
		pConn->idGen.back(callId);
		SAFE_DELETE(it->second)
	}
	pConn->mapCall.clear();
}

unsigned int IOWorker::getBusyLevel()
{
	return m_mapConn.size();
}

Conn *IOWorker::genConn()
{
	unsigned int connId;
	if (!m_connIdGen.generate(connId))
		return NULL;
	Conn *pConn = new Conn();
	pConn->connId = connId;
	m_mapConn[connId] = pConn;
	return pConn;
}

evutil_socket_t IOWorker::getNotify_fd()
{
	return m_notify_fd;
}

void IOWorker::rpcCallback(google::protobuf::Closure *pClosure, evutil_socket_t sync_write_fd)
{
	if (NULL != pClosure)
		//异步回调用户函数
		pClosure->Run();
	else
	{
		//唤醒同步等待中的用户调用线程
		char buf[1];
		send(sync_write_fd, buf, 1, 0);
	}
}