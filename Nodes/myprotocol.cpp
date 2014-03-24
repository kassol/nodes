#include "myprotocol.h"

MsgType MyProtocol::GetMsgType(const char* szMsg)
{
	return MT_ERROR;
}

char* MyProtocol::EncodeMsg(MsgType mt, const char* szMsg)
{
	return "";
}

char* MyProtocol::DecodeMsg(const char* szMsg)
{
	return "";
}

