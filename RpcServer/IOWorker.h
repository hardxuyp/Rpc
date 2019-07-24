#ifndef _IOWORKER_H_
#define _IOWORKER_H_

#include "RpcServerGlobal.h"
#include "BusinessWorker.h"

/************************************************************************
功  能：libevent 描述符可读后回调此函数
参  数：见libevent event_callback_fn描述
返回值：无
************************************************************************/
void notifiedCallback(evutil_socket_t fd, short events, void *pArg);

/************************************************************************
功  能：libevent bufferevent输入缓冲区可读后回调此函数
参  数：见libevent bufferevent_data_cb描述
返回值：无
************************************************************************/
void readCallback(struct bufferevent *pBufEv, void *pArg);

/************************************************************************
功  能：libevent bufferevent上发生可读、可写之外的事件后回调此函数
参  数：见libevent bufferevent_event_cb描述
返回值：无
************************************************************************/
void eventCallback(struct bufferevent *pBufEv, short events, void *pArg);

//IOWorker，包含一个线程，事件循环运行在线程中
class IOWorker
{
public:
	/************************************************************************
	功  能：构造方法
	参  数：
		fds：输入，IOWorker对应的socketpair
		acceptQueueMaxSize：输入，accept队列最大长度
		writeQueueMaxSize：输入，write队列最大长度
		pVecBusinessWorker：输入，业务Worker池指针
	返回值：无
	************************************************************************/
	IOWorker(evutil_socket_t *fds, unsigned int acceptQueueMaxSize, unsigned int completeQueueMaxSize, const vector<BusinessWorker *> *pVecBusinessWorker);
	
	/************************************************************************
	功  能：析构方法
	参  数：无
	返回值：无
	************************************************************************/
	~IOWorker();

	/************************************************************************
	功  能：启动IOWorker
	参  数：无
	返回值：无
	************************************************************************/
	void start();

	/************************************************************************
	功  能：处理accept线程发来的接管连接通知
	参  数：无
	返回值：无
	************************************************************************/
	void handleAccept();

	/************************************************************************
	功  能：处理业务Worker发来的发送响应数据通知
	参  数：无
	返回值：无
	************************************************************************/
	void handleWrite();

	/************************************************************************
	功  能：处理结束通知
	参  数：无
	返回值：无
	************************************************************************/
	void handleEnd();

	/************************************************************************
	功  能：处理bufferevent输入缓冲区数据
	参  数：
		pConn：输入，连接指针
	返回值：无
	************************************************************************/
	void handleRead(Conn *pConn);

	/************************************************************************
	功  能：处理bufferevent可读、可写之外的事件
	参  数：
		pConn：输入，连接指针
	返回值：无
	************************************************************************/
	void handleEvent(Conn *pConn);

	/************************************************************************
	功  能：获取IOWorker的繁忙程度，以当前IOWorker接管的连接数表示繁忙程度
	参  数：无
	返回值：IOWorker的繁忙程度
	************************************************************************/
	unsigned int getBusyLevel();

	/************************************************************************
	功  能：获取IOWorker的socketpair的写端描述符
	参  数：无
	返回值：IOWorker的socketpair的写端描述符
	************************************************************************/
	evutil_socket_t getNotify_fd();

	//accept队列
	SyncQueue<evutil_socket_t> m_acceptQueue;
	//write队列
	SyncQueue<BusinessTask *> m_writeQueue;

private:
	/************************************************************************
	功  能：线程主方法
	参  数：无
	返回值：无
	************************************************************************/
	void threadMain(evutil_socket_t notified_fd);

	/************************************************************************
	功  能：从业务Worker池中调度一个业务Worker用来处理请求（不包括PING心跳）
	参  数：无
	返回值：选中的业务Worker指针，若为NULL，表示调度失败
	************************************************************************/
	BusinessWorker *schedule();

	/************************************************************************
	功  能：检查释放连接
	参  数：
		pConn：连接指针
	返回值：
		true：释放连接成功
		false：释放连接失败
	************************************************************************/
	bool checkToFreeConn(Conn *pConn);

	//线程
	boost::thread m_thd;
	//socketpair读端描述符
	evutil_socket_t m_notified_fd;
	//socketpair写端描述符
	evutil_socket_t m_notify_fd;
	//业务Worker池指针
	const vector<BusinessWorker *> *m_pVecBusinessWorker;
	//所有连接：map<连接描述符, 连接指针>
	map<evutil_socket_t, Conn *> m_mapConn;
	//当前连接数量
	unsigned int m_connNum;
	//event_base指针
	event_base *m_pEvBase;
	//IOWorker是否已经开始启动
	bool m_bStarted;
	//IOWorker是否已经开始结束
	bool m_bEnded;
};

#endif