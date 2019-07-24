#include "IOWorker.h"

IOWorker::IOWorker(evutil_socket_t *fds, unsigned int acceptQueueMaxSize, unsigned int completeQueueMaxSize, const vector<BusinessWorker *> *pVecBusinessWorker)
{
	m_notified_fd = fds[0];
	m_notify_fd = fds[1];
	m_pVecBusinessWorker = pVecBusinessWorker;
	m_pEvBase = NULL;
	m_connNum = 0;
	m_bStarted = false;
	m_bEnded = false;
	m_acceptQueue.setMaxSize(acceptQueueMaxSize);
	m_writeQueue.setMaxSize(completeQueueMaxSize);
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

	if (NOTIFY_IOWORKER_ACCEPT == buf[0])
		pWorker->handleAccept();
	else if (NOTIFY_IOWORKER_WRITE == buf[0])
		pWorker->handleWrite();
	else if (NOTIFY_IOWORKER_END == buf[0])
		pWorker->handleEnd();
}

void IOWorker::handleAccept()
{
	if (NULL == m_pEvBase)
		return;

	//为避免过多的锁操作，一次性从accept队列中取出所有连接描述符
	list<evutil_socket_t> queue;
	m_acceptQueue.takeAll(queue);

	for (auto it = queue.begin(); it != queue.end(); ++it)
	{
		//创建bufferevent
		bufferevent *pBufEv = bufferevent_socket_new(m_pEvBase, *it, BEV_OPT_CLOSE_ON_FREE);
		if (NULL == pBufEv)
			continue;

		//创建连接信息
		Conn *pConn = new Conn();
		pConn->fd = *it;
		pConn->pBufEv = pBufEv;
		pConn->pWorker = this;

		m_mapConn[*it] = pConn;
		++m_connNum;

		//设置bufferevent的回调函数
		bufferevent_setcb(pBufEv, readCallback, NULL, eventCallback, pConn);
		//使能bufferevent上的可读事件
		bufferevent_enable(pBufEv, EV_READ);

		cout << "IO thread " << boost::this_thread::get_id() << " begins serving connnection " << pConn->fd << "..." <<endl;
	}
}

void IOWorker::handleWrite()
{
	//为避免过多的锁操作，一次性从write队列中取出所有业务任务指针
	list<BusinessTask *> queue;
	m_writeQueue.takeAll(queue);

	for (auto it = queue.begin(); it != queue.end(); ++it)
	{
		BusinessTask *pTask = *it;
		Conn *pConn = NULL;
		//将业务任务指针和智能指针绑定，以自动进行销毁和资源释放
		unique_ptr<BusinessTask, function<void(BusinessTask *)> > ptrTask(pTask, [this, &pConn](BusinessTask *pTask) {
			if (NULL == pTask)
				return;
			if (NULL != pTask->pBuf)
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
			}
			SAFE_DELETE(pTask)
			checkToFreeConn(pConn);
		});

		if (NULL == pTask)
			continue;
		//找连接描述符
		auto itFind = m_mapConn.find(pTask->conn_fd);
		if (itFind == m_mapConn.end())
			continue;
		//获取连接信息指针
		pConn = itFind->second;
		if (NULL == pConn)
			continue;
		if (NULL == pTask->pBuf || !pConn->bValid || NULL == pConn->pBufEv)
		{
			--(pConn->todoCount);
			continue;
		}
		//获取bufferevent上的输出evbuffer指针
		evbuffer *pOutBuf = bufferevent_get_output(pConn->pBufEv);
		if (NULL == pOutBuf)
		{
			--(pConn->todoCount);
			continue;
		}
		//添加协议head
		unsigned char arr[HEAD_SIZE] = {0};
		arr[0] = DATA_TYPE_RESPONSE;
		bodySize_t bodyLen = evbuffer_get_length(pTask->pBuf);
		memcpy(arr + 1, &bodyLen, HEAD_SIZE - 1);
		//将协议头放进输出evbuffer
		evbuffer_add(pOutBuf, arr, HEAD_SIZE);
		//将序列化后的协议body移动进输出evbuffer
		evbuffer_remove_buffer(pTask->pBuf, pOutBuf, evbuffer_get_length(pTask->pBuf));
		--(pConn->todoCount);
	}
}

void IOWorker::handleEnd()
{
	//不允许IOWorker重复结束
	if (m_bEnded)
		return;
	m_bEnded = true;

	//满足条件则退出事件循环
	if (m_mapConn.empty() && NULL != m_pEvBase)
		event_base_loopexit(m_pEvBase, NULL);
}

void readCallback(bufferevent *pBufEv, void *pArg)
{
	if (NULL == pBufEv || NULL == pArg)
		return;
	IOWorker *pWorker = ((Conn *)pArg)->pWorker;
	if (NULL != pWorker)
		pWorker->handleRead((Conn *)pArg);
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
			//若数据类型为PING心跳，则直接回复客户端PONG心跳
			if (DATA_TYPE_HEARTBEAT_PING == pArr[0])
			{
				//获取bufferevent中的输出evbuffer指针
				evbuffer *pOutBuf = bufferevent_get_output(pBufEv);
				if (NULL != pOutBuf)
				{
					//创建PONG心跳的协议head，协议body长度为0
					unsigned char arr[HEAD_SIZE] = {0};
					arr[0] = DATA_TYPE_HEARTBEAT_PONG;
					//将协议数据放进输出evbuffer
					evbuffer_add(pOutBuf, arr, sizeof(arr));

					cout << "IO thread " << boost::this_thread::get_id() << " finishes replying PONG heartbeat." << endl;
				}
				//从输入evbuffer中移出协议数据
				evbuffer_drain(pInBuf, HEAD_SIZE + pConn->inBodySize);
			}
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

			//创建业务任务
			BusinessTask *pTask = new BusinessTask();
			pTask->pWorker = this;
			pTask->conn_fd = pConn->fd;
			//创建evbuffer用来移动输入evbuffer的内容
			pTask->pBuf = evbuffer_new();
			if (NULL != pTask->pBuf)
			{
				//调度业务Worker处理请求
				BusinessWorker *pSelectedWorker = schedule();
				if (NULL != pSelectedWorker)
				{
					evbuffer_remove_buffer(pInBuf, pTask->pBuf, pConn->inBodySize);
					//将业务任务指针放入选中的业务Worker中的队列
					if (pSelectedWorker->m_queue.put(pTask))
					{
						++(pConn->todoCount);
						evbuffer_drain(pInBuf, pConn->inBodySize);
						pConn->inState = PROTOCOL_HEAD;
						continue;
					}
					//以下分支都表示未成功将业务任务派发给业务Worker，均必须销毁内存、释放资源
					SAFE_DELETE(pTask)
				}
				else
				{
					evbuffer_free(pTask->pBuf);
					SAFE_DELETE(pTask)
				}
			}
			else
				SAFE_DELETE(pTask)
			
			evbuffer_drain(pInBuf, pConn->inBodySize);
			pConn->inState = PROTOCOL_HEAD;
		}
		else
			break;
	}
}

void eventCallback(struct bufferevent *pBufEv, short events, void *pArg)
{
	//events：1、连接上；2、eof；3、超时；4、各种错误
	if (NULL == pBufEv || NULL == pArg)
		return;
	IOWorker *pWorker = ((Conn *)pArg)->pWorker;
	if (NULL != pWorker)
		pWorker->handleEvent((Conn *)pArg);
}

void IOWorker::handleEvent(Conn *pConn)
{
	if (NULL == pConn)
		return;

	pConn->bValid = false;
	checkToFreeConn(pConn);
}

BusinessWorker *IOWorker::schedule()
{
	BusinessWorker *pSelectedWorker = NULL;
	unsigned int minBusyLevel;

	auto it = m_pVecBusinessWorker->begin();
	for ( ; it != m_pVecBusinessWorker->end(); ++it)
	{
		BusinessWorker *pWorker = *it;
		if (NULL != pWorker)
		{
			pSelectedWorker = pWorker;
			//获取业务Worker的繁忙程度，越不忙的业务Worker越容易被选中
			minBusyLevel = pSelectedWorker->getBusyLevel();
			break;
		}
	}
	if (NULL == pSelectedWorker || minBusyLevel <= 0)
		return pSelectedWorker;

	for (++it; it != m_pVecBusinessWorker->end(); ++it)
	{
		BusinessWorker *pWorker = *it;
		if (NULL != pWorker)
		{
			unsigned int busyLevel = pWorker->getBusyLevel();
			if (busyLevel < minBusyLevel)
			{
				pSelectedWorker = pWorker;
				minBusyLevel = busyLevel;
				//若业务Worker完全空闲，则立即挑选该业务Worker
				if (minBusyLevel <= 0)
					break;
			}
		}
	}
	return pSelectedWorker;
}

bool IOWorker::checkToFreeConn(Conn *pConn)
{
	int i;
	unique_ptr<int, function<void(int *)> > ptrMonitor(&i, [this](int *p) {
		//满足条件则退出事件循环
		if (m_bEnded && m_mapConn.empty() && NULL != m_pEvBase)
			event_base_loopexit(m_pEvBase, NULL);
	});

	if (NULL == pConn)
		return false;

	//若连接上没有尚未处理的请求（不包括PING心跳），则销毁连接
	if (!pConn->bValid && pConn->todoCount <= 0 && NULL != pConn->pBufEv)
	{
		auto it = m_mapConn.find(pConn->fd);
		if (it != m_mapConn.end())
			m_mapConn.erase(it);

		bufferevent_free(pConn->pBufEv);
		SAFE_DELETE(pConn)
		return true;
	}
	return false;
}

unsigned int IOWorker::getBusyLevel()
{
	return m_connNum;
}

evutil_socket_t IOWorker::getNotify_fd()
{
	return m_notify_fd;
}