#ifndef _RPCSERVERDEFINE_H_
#define _RPCSERVERDEFINE_H_

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <boost/thread/thread.hpp>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include "SyncQueue.h"
#include "Global.h"
#ifdef WIN32
#include <winsock2.h>
#endif

using namespace std;

//向RpcServer发送的通知类型
enum NOTIFY_SERVER_TYPE
{
	//结束：通知RpcServer结束事件循环
	NOTIFY_SERVER_END = 0
};

//向IOWorker发送的通知类型
enum NOTIFY_IOWORKER_TYPE
{
	//接管连接：由accept线程accept连接，然后通知某个IOWorker接管连接
	NOTIFY_IOWORKER_ACCEPT = 0, 
	//发送数据：由业务Worker创建响应数据，然后通知相应IOWorker发送响应数据
	NOTIFY_IOWORKER_WRITE = 1, 
	//结束：通知IOWorker优雅结束
	NOTIFY_IOWORKER_END = 2
};

class IOWorker;
//服务端连接
struct Conn
{
	//当前连接上的输入数据状态
	PROTOCOL_PART inState;
	//当前连接上的输入数据的body长度
	bodySize_t inBodySize;

	//连接描述符
	evutil_socket_t fd;
	//连接对应的buffer事件
	bufferevent *pBufEv;
	//连接所属的IOWorker
	IOWorker *pWorker;
	//连接上尚未处理的请求（不包括PING心跳）数量
	unsigned int todoCount;
	//连接是否有效
	bool bValid;

	Conn()
	{
		inState = PROTOCOL_HEAD;
		inBodySize = 0;
		pBufEv = NULL;
		pWorker = NULL;
		todoCount = 0;
		bValid = true;
	}
};

//业务任务
struct BusinessTask
{
	//发起业务任务的IOWorker
	IOWorker *pWorker;
	//发起业务任务的连接描述符
	evutil_socket_t conn_fd;
	//发起业务任务的连接的临时evbuffer
	evbuffer *pBuf;

	BusinessTask()
	{
		pWorker = NULL;
		pBuf = NULL;
	}
};

#endif