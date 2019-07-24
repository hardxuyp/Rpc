#include "RpcChannel.h"

IRpcChannel::~IRpcChannel() {}

IRpcChannel *IRpcChannel::createRpcChannel(IRpcClient *pIClient, const string &ip, int port)
{
	return new ::RpcChannel((RpcClient *)pIClient, ip, port);
}

void IRpcChannel::releaseRpcChannel(IRpcChannel *pIRpcChannel)
{
	SAFE_DELETE(pIRpcChannel)
}

RpcChannel::RpcChannel(RpcClient *pClient, const string &ip, int port)
{
	m_pClient = pClient;
	m_pWorker = NULL;
	m_ip = ip;
	m_port = port;
	m_bConnected = false;
	m_bSyncValid = false;

	//创建用于同步调用的socketpair
	evutil_socket_t fds[2];
	if (evutil_socketpair(AF_INET, SOCK_STREAM, 0, fds) >= 0)
	{
		//socketpair的2个描述符默认阻塞
		m_sync_read_fd = fds[0];
		m_sync_write_fd = fds[1];
		m_bSyncValid = true;
	}
}

RpcChannel::~RpcChannel()
{
	//关闭用于同步调用的socketpair的描述符
	if (m_bSyncValid)
	{
		evutil_closesocket(m_sync_read_fd);
		evutil_closesocket(m_sync_write_fd);
	}

	//通知IOWorker断开连接
	if (m_bConnected && NULL != m_pWorker)
	{
		IOTask task;
		task.type = IOTask::DISCONNECT;
		task.pData = new unsigned int(m_connId);

		m_pWorker->m_queue.put(task);
		char buf[1] = { NOTIFY_IOWORKER_IOTASK };
		send(m_pWorker->getNotify_fd(), buf, 1, 0);
	}
}

void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
	google::protobuf::RpcController* controller,
	const google::protobuf::Message* request,
	google::protobuf::Message* response,
	google::protobuf::Closure* done)
{
	if (NULL != controller)
		controller->Reset();

	if (NULL == method || NULL == request || NULL == response || !m_bSyncValid)
	{
		if (NULL != controller)
			controller->SetFailed("method == NULL or request == NULL or response == NULL or sync socketpair created failed");
		return;
	}

	if (NULL == m_pClient)
	{
		if (NULL != controller)
			controller->SetFailed("RpcClient object does not exist");
		return;
	}

	//未发起过连接
	if (!m_bConnected)
	{
		//让RpcClient调度IOWorker池并在选中的IOWorker上分配连接
		Conn *pConn = NULL;
		m_pWorker = m_pClient->schedule(pConn);
		if (NULL == m_pWorker)
		{
			//调度失败
			if (NULL != controller)
				controller->SetFailed("connection refused");
			return;
		}
		m_connId = pConn->connId;
		//通知IOWorker连接
		pConn->pWorker = m_pWorker;
		//创建服务器地址
		pConn->serverAddr.sin_family = AF_INET;
		if (0 == evutil_inet_pton(AF_INET, m_ip.c_str(), &(pConn->serverAddr.sin_addr)))
			return;
		pConn->serverAddr.sin_port = htons(m_port);

		IOTask task;
		task.type = IOTask::CONNECT;
		task.pData = pConn;

		m_pWorker->m_queue.put(task);
		char buf[1] = { NOTIFY_IOWORKER_IOTASK };
		send(m_pWorker->getNotify_fd(), buf, 1, 0);
		m_bConnected = true;
	}
	//序列化请求参数
	string *pStr = new string();
	if (!request->SerializeToString(pStr))
	{
		if (NULL != controller)
			controller->SetFailed("request serialized failed");
		SAFE_DELETE(pStr)
		return;
	}
	//通知IOWorker调用
	Call *pCall = new Call();
	pCall->connId = m_connId;
	pCall->sync_write_fd = m_sync_write_fd;
	pCall->pStrReq = pStr;
	pCall->pRespMessage = response;
	pCall->pClosure = done;
	pCall->pController = controller;
	pCall->serviceName = method->service()->full_name();
	pCall->methodIndex = method->index();

	IOTask task;
	task.type = IOTask::CALL;
	task.pData = pCall;

	m_pWorker->m_queue.put(task);
	char buf[1] = { NOTIFY_IOWORKER_IOTASK };
	send(m_pWorker->getNotify_fd(), buf, 1, 0);

	//若为同步调用，通过读阻塞的读端描述符使本线程阻塞，直到调用返回时向写端描述符写入
	//若为异步调用，直接返回，调用返回时会调用回调函数
	if (NULL == done)
	{
		char buf[1];
		recv(m_sync_read_fd, buf, 1, 0);
	}
}