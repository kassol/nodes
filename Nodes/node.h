#pragma once

#define _WIN32_WINDOWS 0x0501

#define _CRT_SECURE_NO_WARNINGS

#include "mymsg.h"
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/thread.hpp>
#include <boost/filesystem.hpp>
#include <fstream>
#include <iostream>
#include <string>
#include <ctime>
#include <vector>


void log(const char* p);


struct task_struct 
{
	task_struct(std::string task, unsigned int state)
		: task_(task)
		, state_(state)
	{
		ip_ = "";
	}

	std::string task_;
	unsigned int state_;
	std::string ip_;
};

struct node_struct
{
	node_struct(std::string ip)
		: ip_(ip)
		, is_busy(false)
	{

	}
	std::string ip_;
	bool is_busy;

	bool operator==(const node_struct& node)
	{
		return node.ip_==ip_;
	}
};

struct msg_struct
{
	msg_struct(MsgType mt, std::string msg, std::string ip)
		: mt_(mt)
		, msg_(msg)
		, ip_(ip)
	{

	}
	msg_struct(MsgType mt, std::string msg)
		: mt_(mt)
		, msg_(msg)
	{
		ip_ = "";
	}
	MsgType mt_;
	std::string msg_;
	std::string ip_;
};

class node : public boost::enable_shared_from_this<node>
	, boost::noncopyable
{

public:
	enum NodeType{NT_MASTER, NT_NORMAL};
	friend class session;

public:
	node(boost::asio::io_service& io_service, unsigned short port)
		: nt_(NT_NORMAL)
		, ip_("")
		, master_ip("")
		, io_service_(io_service)
		, listen_port(port)
		, acceptor_(io_service, 
		boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
		, file_acceptor_(io_service)
		, limit_filenum_to_transfer(2)
		, cur_filenum(0)
		, is_scan_finished(true)
		, is_ping_busy(false)
		, is_busy(false)
	{
		is_connected = Initialize();
		if (is_connected)
		{
			start_accept();
		}
		else
		{
			log("Don't have available ip");
		}
	}

	void Start();
	bool IsMaster();

private:
	bool Initialize();
	void ParseProj();

private:
	void start_accept();
	void start_scan();
	void start_ping();
	void handle_accept(session* new_session, const boost::system::error_code& error);
	void handle_connect(session* new_session, const boost::system::error_code& error);
	void handle_connect(session* new_session, msg_struct* msg,
		const boost::system::error_code& error);
	void handle_msg(std::string ip, MyMsg msg);

private:
	NodeType nt_;
	std::string ip_;
	std::string master_ip;
	unsigned short listen_port;
	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;
	boost::asio::ip::tcp::acceptor file_acceptor_;

	bool is_busy;
	bool is_connected;
	bool is_scan_finished;
	bool is_ping_busy;

	unsigned int limit_filenum_to_transfer;
	unsigned int cur_filenum;


	std::vector<node_struct> available_list;
	std::vector<task_struct> task_list_;
};


enum SessionType
{
	ST_NORMAL,
	ST_METAFILE,
	ST_FILE,
	ST_FILE_BACK
};

class session
{
public:
	session(boost::asio::io_service& io_service, node* owner, SessionType st = ST_NORMAL)
		: socket_(io_service)
		, owner_(owner)
		, st_(st)
	{
		
	}

	~session()
	{
		socket_.close();
	}

	boost::asio::ip::tcp::socket& socket()
	{
		return socket_;
	}

	void handle_accept()
	{
		boost::asio::async_read(socket_,
			boost::asio::buffer(msg_.data(), MyMsg::header_length),
			boost::bind(&session::handle_read_header, this,
			boost::asio::placeholders::error));
	}

	void send_msg(MsgType mt, const char* szbuf)
	{
		if (msg_.encode_body(mt, szbuf))
		{
			msg_.encode_header();
			boost::asio::async_write(socket_,
				boost::asio::buffer(msg_.data(),
				msg_.length()),
				boost::bind(&session::handle_write, this,
				boost::asio::placeholders::error));
		}
		else
		{
			delete this;
		}
	}

private:
	void handle_read_header(const boost::system::error_code& error)
	{
		if (!error && msg_.decode_header())
		{
			boost::asio::async_read(socket_,
				boost::asio::buffer(msg_.body(), msg_.body_length()),
				boost::bind(&session::handle_read_body, this,
				boost::asio::placeholders::error));
		}
		else
		{
			log(error.message().c_str());
			delete this;
		}
	}

	void handle_read_body(const boost::system::error_code& error)
	{
		if (!error)
		{
// 			std::cout.write(msg_.body(), msg_.body_length());
// 			std::cout<<"\n";
			log(msg_.body());
			log("Read over");
			boost::system::error_code ec;
			boost::asio::ip::tcp::endpoint ep = socket_.remote_endpoint(ec);
			if (!ec)
			{
				owner_->handle_msg(ep.address().to_string(), msg_);
			}
			else
			{
				log(error.message().c_str());
			}
		}
		else
		{
			log(error.message().c_str());
		}
		delete this;
	}

	void handle_write(const boost::system::error_code& error)
	{
		if (!error)
		{
			log("Write over");
		}
		else
		{
			log(error.message().c_str());
		}
		delete this;
	}

private:
	boost::asio::ip::tcp::socket socket_;
	MyMsg msg_;
	enum{max_data_block = 1024};
	char data_buf[max_data_block];
	SessionType st_;
	node* owner_;
};

