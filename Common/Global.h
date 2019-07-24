#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include "RpcController.h"
#include "ProtocolBody.pb.h"

#define SAFE_DELETE(p) { if (NULL != (p)) { delete (p); (p) = NULL; } }

/************************************************************************
Rpc通信协议结构：head + body
1、head：长度为5字节
	（1）第1字节：数据类型，见下面DATA_TYPE的定义
	（2）第2-5字节：body的长度
2、body
	（1）若数据类型为心跳（PING、PONG），body长度为0字节
	（2）若数据类型为请求或响应，body内容见ProtocolBody.proto
************************************************************************/

//协议head长度
#define HEAD_SIZE 5
//协议body长度类型
typedef uint32_t bodySize_t;
//调用Id类型
typedef uint32_t callId_t;

//协议部位
enum PROTOCOL_PART
{
	//head
	PROTOCOL_HEAD = 0, 
	//body
	PROTOCOL_BODY = 1
};

//协议数据类型
enum DATA_TYPE
{
	//请求，由客户端发送给服务器
	DATA_TYPE_REQUEST = 0, 
	//响应，由服务器回复给客户端
	DATA_TYPE_RESPONSE = 1, 
	//PING心跳，由客户端发送给服务器
	DATA_TYPE_HEARTBEAT_PING = 2, 
	//PONG心跳，由服务器回复给客户端
	DATA_TYPE_HEARTBEAT_PONG = 3
};

#endif