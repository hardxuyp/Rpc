#ifndef _RPCCONTROLLER_H_
#define _RPCCONTROLLER_H_

#include <string>
#include <google/protobuf/service.h>

#ifdef WIN32
#ifdef RPCSERVER_EXPORTS
#define RPCCONTROLLER_DLL_EXPORTS __declspec(dllexport)
#else
#ifdef RPCCLIENT_EXPORTS
#define RPCCONTROLLER_DLL_EXPORTS __declspec(dllexport)
#else
#define RPCCONTROLLER_DLL_EXPORTS __declspec(dllimport)
#endif
#endif
#else
#define RPCCONTROLLER_DLL_EXPORTS
#endif

class RPCCONTROLLER_DLL_EXPORTS RpcController : public google::protobuf::RpcController
{
public:
	RpcController();
	virtual void Reset();
	virtual bool Failed() const;
	virtual std::string ErrorText() const;
	virtual void StartCancel();
	virtual void SetFailed(const std::string& reason);
	virtual bool IsCanceled() const;
	virtual void NotifyOnCancel(google::protobuf::Closure* callback);

private:
	std::string m_strError;
	bool m_bFailed;
};

#endif