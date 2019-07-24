#ifndef _RPCCLIENTGLOBAL_H_
#define _RPCCLIENTGLOBAL_H_

#include <iostream>
#include <string>
#include <map>
#include <functional>
#include <boost/thread/thread.hpp>
#include <google/protobuf/service.h>
#include <google/protobuf/descriptor.h>
#include <event2/util.h>
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include "SyncQueue.h"
#include "UniqueIdGenerator.h"
#include "Global.h"
#include "ProtocolBody.pb.h"
#ifdef WIN32
#include <winsock2.h>
#endif

using namespace std;

//向IOWorker发送的通知类型
enum NOTIFY_IOWORKER_TYPE
{
	//IO任务：通知IOWorker处理IO任务，任务类型包括连接、断开连接、调用
	NOTIFY_IOWORKER_IOTASK = 0, 
	//结束：通知IOWorker优雅结束
	NOTIFY_IOWORKER_END = 1
};

//客户端调用
struct Call
{
	//调用id
	callId_t callId;
	//连接id
	unsigned int connId;
	//用于同步调用的socketpair写端描述符
	evutil_socket_t sync_write_fd;
	//请求参数序列化后的内存指针
	string *pStrReq;
	//响应的Message指针
	google::protobuf::Message *pRespMessage;
	//用于异步回调的Closure指针
	google::protobuf::Closure *pClosure;
	//用于表示成功与否及错误原因的RpcController指针
	google::protobuf::RpcController *pController;
	//调用的服务名称
	string serviceName;
	//调用的方法索引下标
	uint32_t methodIndex;

	Call()
	{
		pStrReq = NULL;
		pRespMessage = NULL;
		pClosure = NULL;
		pController = NULL;
	}
};

class IOWorker;
//客户端连接
struct Conn
{
	//当前连接上的输入数据状态
	PROTOCOL_PART inState;
	//当前连接上的输入数据的body长度
	bodySize_t inBodySize;

	//是否连接成功
	bool bConnected;
	//是否主观认为失去连接
	bool bConnectionMightLost;
	//连接id
	unsigned int connId;
	//连接描述符
	evutil_socket_t fd;
	//连接对应的buffer事件
	bufferevent *pBufEv;
	//唯一调用id生成器
	UniqueIdGenerator<callId_t> idGen;
	//所有调用
	map<callId_t, Call *> mapCall;
	//连接所属的IOWorker
	IOWorker *pWorker;
	//服务器监听的地址
	sockaddr_in serverAddr;

	Conn()
	{
		inState = PROTOCOL_HEAD;
		inBodySize = 0;
		bConnected = false;
		bConnectionMightLost = false;
		pBufEv = NULL;
		pWorker = NULL;
	}
};

//IO任务
struct IOTask
{
	//IO任务类型定义
	enum TYPE
	{
		//连接
		CONNECT = 0, 
		//断开连接
		DISCONNECT = 1, 
		//调用
		CALL = 2
	};
	//IO任务类型
	IOTask::TYPE type;
	//任务数据指针，必须根据type进行不同的类型转换
	void *pData;

	IOTask()
	{
		pData = NULL;
	}
};

#endif