#pragma once


enum MsgType
{
	MT_MASTER,
	MT_AVAILABLE,
	MT_OCCUPIED,
	MT_METAFILE,
	MT_METAFILE_READY,
	MT_METAFILE_FINISH,
	MT_METAFILE_FAIL,
	MT_FILE_REQUEST,
	MT_FILE,
	MT_FILE_READY,
	MT_FILE_FINISH, 
	MT_FILE_FAIL,
	MT_FILE_BACK,
	MT_FILE_BACK_READY,
	MT_FILE_BACK_FINISH,
	MT_FILE_BACK_FAIL,
	MT_PING,
	MT_PING_BACK,
	MT_ERROR
};


class MyProtocol
{
	static MsgType GetMsgType(const char* szMsg);
	static char* EncodeMsg(MsgType mt, const char* szMsg);
	static char* DecodeMsg(const char* szMsg);
};
