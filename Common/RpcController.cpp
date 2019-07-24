#include "RpcController.h"

RpcController::RpcController()
{
	Reset();
}

void RpcController::Reset()
{
	m_strError.clear();
	m_bFailed = false;
}

bool RpcController::Failed() const
{
	return m_bFailed;
}

std::string RpcController::ErrorText() const
{
	return m_strError;
}

void RpcController::StartCancel()
{
}

void RpcController::SetFailed(const std::string& reason)
{
	m_bFailed = true;
	m_strError = reason;
}

bool RpcController::IsCanceled() const
{
	return true;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback)
{
}