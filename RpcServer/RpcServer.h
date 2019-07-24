#ifndef _RPCSERVER_H_
#define _RPCSERVER_H_

#include "RpcServerGlobal.h"
#include "IOWorker.h"
#include "BusinessWorker.h"
#include "IRpcServer.h"

/************************************************************************
服务端功能结构描述：
1、accept线程accept连接；
2、accept线程调度IOWorker池中的IOWorker接管连接，每个IOWorker可接管多个连接；
3、IOWorker调度业务Worker池中的业务Worker处理请求，得到的响应再由相应IOWorker发回客户端。
************************************************************************/

/************************************************************************
功  能：libevent 描述符可读后回调此函数
参  数：见libevent event_callback_fn描述
返回值：无
************************************************************************/
void serverNotifiedCallback(evutil_socket_t fd, short events, void *pArg);

/************************************************************************
功  能：libevent accept连接后回调此函数
参  数：见libevent evconnlistener_cb描述
返回值：无
************************************************************************/
void acceptCallback(evconnlistener *pListener, evutil_socket_t fd, struct sockaddr *pAddr, int socklen, void *pArg);

//服务器
class RpcServer : public IRpcServer
{
public:
	/************************************************************************
	功  能：构造方法
	参  数：
		ip：输入，服务器监听的ip地址
		port：输入，服务器监听的端口号
		IOWorkerNum：输入，IOWorker池Worker数量
		IOWorkerAcceptQueueMaxSize：输入，IOWorker接管连接队列的最大长度
		IOWorkerWriteQueueMaxSize：输入，IOWorker发送队列的最大长度
		businessWorkerNum：输入，业务Worker池Worker数量
		businessWorkerQueueMaxSize：输入，业务Worker业务任务队列的最大长度
	返回值：无
	************************************************************************/
	RpcServer(const string &ip, int port, 
		unsigned int IOWorkerNum, unsigned int IOWorkerAcceptQueueMaxSize, unsigned int IOWorkerCompleteQueueMaxSize, 
		unsigned int businessWorkerNum, unsigned int businessWorkerQueueMaxSize);

	/************************************************************************
	功  能：析构方法
	参  数：无
	返回值：无
	************************************************************************/
	~RpcServer();

	/************************************************************************
	功  能：注册服务
	参  数：
		pService：服务指针
	返回值：无
	************************************************************************/
	virtual void registerService(google::protobuf::Service *pService);

	/************************************************************************
	功  能：启动RpcServer
	参  数：无
	返回值：无
	************************************************************************/
	virtual void start();

	/************************************************************************
	功  能：结束RpcServer
	参  数：无
	返回值：无
	************************************************************************/
	virtual void end();

	/************************************************************************
	功  能：处理结束事件循环的通知
	参  数：无
	返回值：无
	************************************************************************/
	void handleEnd();

	/************************************************************************
	功  能：处理libevent accept的连接
	参  数：
		fd：输入，libevent accept的连接描述符
	返回值：无
	************************************************************************/
	void handleAccept(evutil_socket_t fd);

private:
	/************************************************************************
	功  能：从IOWorker池中调度一个IOWorker用来接管连接
	参  数：无
	返回值：选中的IOWorker指针，若为NULL，表示调度失败
	************************************************************************/
	IOWorker *schedule();

	//服务器监听的ip地址
	string m_ip;
	//服务器监听的端口号
	int m_port;
	//外部注册的所有服务：map<服务名称, pair<服务指针, vector<方法描述指针> > >
	map<string, pair<google::protobuf::Service *, vector<const google::protobuf::MethodDescriptor *> > > m_mapRegisteredService;
	//IOWorker池：map<IOWorker对应的socketpair的写端描述符, IOWorker指针>
	map<evutil_socket_t, IOWorker *> m_mapIOWorker;
	//业务Worker池：vector<业务Worker指针>
	vector<BusinessWorker *> m_vecBusinessWorker;
	//event_base指针
	event_base *m_pEvBase;
	//socketpair的读端描述符，用于接收server结束的通知
	evutil_socket_t m_notified_fd;
	//socketpair的写端描述符，用于通知server结束
	evutil_socket_t m_notify_fd;
	//RpcServer是否已经开始启动
	bool m_bStarted;
	//RpcServer是否已经开始结束
	bool m_bEnded;
};

#endif