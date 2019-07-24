#include "BusinessWorker.h"
#include "IOWorker.h"

BusinessWorker::BusinessWorker(unsigned int queueMaxSize)
{
	m_bEnded = false;
	m_pMapRegisteredService = NULL;
	m_queue.setMaxSize(queueMaxSize);
	m_queue.setWait(true);
}

BusinessWorker::~BusinessWorker()
{
	m_bEnded = true;
	m_queue.stop();
	//等待线程结束
	m_thd.join();
}

void BusinessWorker::setRegisteredServices(const map<string, pair<google::protobuf::Service *, vector<const google::protobuf::MethodDescriptor *> > > *pMapRegisteredService)
{
	m_pMapRegisteredService = pMapRegisteredService;
}

void BusinessWorker::start()
{
	//启动线程
	m_thd = std::move(boost::thread(&BusinessWorker::threadMain, this));
}

unsigned int BusinessWorker::getBusyLevel()
{
	return m_queue.getSize();
}

void BusinessWorker::threadMain()
{
	while (!m_bEnded)
	{
		//为避免过多的锁操作，一次性从队列中取出所有业务任务指针
		list<BusinessTask *> queue;
		m_queue.takeAll(queue);

		for (auto it = queue.begin(); it != queue.end(); ++it)
		{
			cout << "business thread " << boost::this_thread::get_id() << " begins handling task..." << endl;

			BusinessTask *pTask = *it;
			//将业务任务指针和智能指针绑定，以让通知IOWorker自动执行
			unique_ptr<BusinessTask, function<void(BusinessTask *)> > ptrMonitor(pTask, [](BusinessTask *pTask) {
				if (NULL == pTask)
					return;
				if (NULL == pTask->pWorker)
					return;
				//将业务任务指针放入相应IOWorker的write队列，且放不成功就一直等待
				while (!pTask->pWorker->m_writeQueue.put(pTask)) {}
				//通知相应IOWorker
				char buf[1] = { NOTIFY_IOWORKER_WRITE };
				send(pTask->pWorker->getNotify_fd(), buf, 1, 0);
			});

			if (NULL == pTask)
				continue;
			if (NULL == pTask->pBuf)
				continue;

			if (NULL == m_pMapRegisteredService)
			{
				//释放evbuffer
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}

			//获取请求的协议body长度
			bodySize_t len = evbuffer_get_length(pTask->pBuf);
			//反序列化请求的协议body
			ProtocolBodyRequest bodyReq;
			if (!bodyReq.ParseFromArray(evbuffer_pullup(pTask->pBuf, len), len))
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//通过服务名找服务信息
			auto itFind = m_pMapRegisteredService->find(bodyReq.servicename());
			if (itFind == m_pMapRegisteredService->end())
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//获取服务指针
			google::protobuf::Service *pService = itFind->second.first;
			uint32_t index = bodyReq.methodindex();
			if (NULL == pService || index >= itFind->second.second.size())
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//获取方法描述指针
			const google::protobuf::MethodDescriptor *pMethodDescriptor = itFind->second.second[index];
			if (NULL == pMethodDescriptor)
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//创建方法入参Message
			google::protobuf::Message *pReq = pService->GetRequestPrototype(pMethodDescriptor).New();
			if (NULL == pReq)
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//将创建的方法入参Message和智能指针绑定，以让创建的方法入参Message自动销毁
			unique_ptr<google::protobuf::Message> ptrReq(pReq);
			//创建方法出参Message
			google::protobuf::Message *pResp = pService->GetResponsePrototype(pMethodDescriptor).New();
			if (NULL == pResp)
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//将创建的方法出参Message和智能指针绑定，以让创建的方法出参Message自动销毁
			unique_ptr<google::protobuf::Message> ptrResp(pResp);
			//方法入参反序列化
			if (!pReq->ParseFromString(bodyReq.content()))
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}

			//方法调用
			RpcController controller;
			pService->CallMethod(pMethodDescriptor, &controller, pReq, pResp, NULL);

			//创建响应的协议body
			ProtocolBodyResponse bodyResp;
			//设置调用id，此调用id由客户端创建并维护，服务端只管原样发回
			bodyResp.set_callid(bodyReq.callid());
			//序列化出参
			string content;
			if (!pResp->SerializeToString(&content))
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}
			//设置序列化后的出参
			bodyResp.set_content(content);
			//序列化响应的协议body
			string strRet;
			if (!bodyResp.SerializeToString(&strRet))
			{
				evbuffer_free(pTask->pBuf);
				pTask->pBuf = NULL;
				continue;
			}

			evbuffer_free(pTask->pBuf);
			//创建evbuffer
			pTask->pBuf = evbuffer_new();
			if (NULL == pTask->pBuf)
				continue;
			//将序列化后的响应的协议body放入evbuffer
			evbuffer_add(pTask->pBuf, strRet.c_str(), strRet.size());

			cout << "business thread " << boost::this_thread::get_id() << " finishes handling task." << endl;
		}
	}
}