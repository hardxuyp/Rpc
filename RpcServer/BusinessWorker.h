#ifndef _BUSINESSWORKER_H_
#define _BUSINESSWORKER_H_

#include "RpcServerGlobal.h"

//业务Worker，包含一个线程，序列化、反序列化、方法调用等都在线程中进行
class BusinessWorker
{
public:
	/************************************************************************
	功  能：构造方法
	参  数：
		queueMaxSize：输入，业务Worker业务任务队列的最大长度
	返回值：无
	************************************************************************/
	BusinessWorker(unsigned int queueMaxSize);

	/************************************************************************
	功  能：析构方法
	参  数：无
	返回值：无
	************************************************************************/
	~BusinessWorker();

	/************************************************************************
	功  能：设置所有注册的服务
	参  数：
		pMapRegisteredService：输入，所有注册的服务指针
	返回值：无
	************************************************************************/
	void setRegisteredServices(const map<string, pair<google::protobuf::Service *, vector<const google::protobuf::MethodDescriptor *> > > *pMapRegisteredService);
	
	/************************************************************************
	功  能：启动业务Worker
	参  数：无
	返回值：无
	************************************************************************/
	void start();

	/************************************************************************
	功  能：获取业务Worker的繁忙程度，以业务任务队列的当前长度表示繁忙程度
	参  数：无
	返回值：业务Worker的繁忙程度
	************************************************************************/
	unsigned int getBusyLevel();

	//业务任务队列
	SyncQueue<BusinessTask *> m_queue;

private:
	/************************************************************************
	功  能：线程主方法
	参  数：无
	返回值：无
	************************************************************************/
	void threadMain();
	
	//线程
	boost::thread m_thd;
	//是否结束线程
	bool m_bEnded;
	//所有注册的服务指针
	const map<string, pair<google::protobuf::Service *, vector<const google::protobuf::MethodDescriptor *> > > *m_pMapRegisteredService;
};

#endif