#pragma once


#define _CRT_SECURE_NO_WARNINGS

#include <string>

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


class MyMsg
{
public:
	enum{header_length = 4};
	enum{max_body_length = 512};

	MyMsg()
		: body_length_(0)
	{

	}

	const char* data()const
	{
		return data_;
	}

	char* data()
	{
		return data_;
	}
	
	size_t length()const
	{
		return header_length+body_length_;
	}

	const char* body()const
	{
		return data_+header_length;
	}

	char* body()
	{
		return data_+header_length;
	}

	size_t body_length()const
	{
		return body_length_;
	}

	void body_length(size_t new_length)
	{
		body_length_ = new_length;
		if (body_length_ > max_body_length)
		{
			body_length_ = max_body_length;
		}
	}

	bool decode_header()
	{
		char header[header_length+1] = "";
		std::strncat(header, data_, header_length);
		body_length_ = atoi(header);
		if (body_length_ > max_body_length)
		{
			body_length_ = 0;
			return false;
		}
		return true;
	}

	void encode_header()
	{
		char header[header_length+1];
		sprintf(header, "%4d", body_length_);
		memcpy(data_, header, header_length);
	}

private:
	char data_[header_length+max_body_length];
	size_t body_length_;
};
