#ifndef _RPCCHANNEL_H_
#define _RPCCHANNEL_H_

#include "RpcClientGlobal.h"
#include "IRpcChannel.h"
#include "RpcClient.h"

//Rpc通道，一个通道代表一个连接
class RpcChannel : public IRpcChannel
{
public:
	/************************************************************************
	功  能：构造方法
	参  数：
		pClient：输入，RpcClient指针
		ip：输入，服务器监听的ip地址
		port：输入，服务器监听的端口号
	返回值：无
	************************************************************************/
	RpcChannel(RpcClient *pClient, const string &ip, int port);

	/************************************************************************
	功  能：析构方法
	参  数：无
	返回值：无
	************************************************************************/
	~RpcChannel();

	/************************************************************************
	功  能：调用方法
	参  数：见google::protobuf::RpcChannel CallMethod()描述
	返回值：无
	************************************************************************/
	virtual void CallMethod(const google::protobuf::MethodDescriptor* method,
		google::protobuf::RpcController* controller,
		const google::protobuf::Message* request,
		google::protobuf::Message* response,
		google::protobuf::Closure* done);

private:
	//RpcClient指针
	RpcClient *m_pClient;
	//IOWorker指针
	IOWorker *m_pWorker;
	//是否发起过连接
	bool m_bConnected;
	//用于同步调用的socketpair是否有效
	bool m_bSyncValid;
	//连接id
	unsigned int m_connId;
	//服务器监听的ip地址
	string m_ip;
	//服务器监听的端口号
	int m_port;
	//用于同步调用的socketpair读端描述符
	evutil_socket_t m_sync_read_fd;
	//用于同步调用的socketpair写端描述符
	evutil_socket_t m_sync_write_fd;
};

#endif