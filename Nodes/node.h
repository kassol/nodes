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
#include <deque>
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

struct file_struct
{
	file_struct(std::string filename, unsigned __int64 filesize)
		: filename_(filename)
		, filesize_(filesize)
	{

	}
	std::string filename_;
	unsigned __int64 filesize_;
};

struct addr_struct
{
	addr_struct(std::string ip, unsigned short port)
		: ip_(ip)
		, port_(port)
	{

	}
	std::string ip_;
	unsigned short port_;
};

class node : public boost::enable_shared_from_this<node>
	, boost::noncopyable
{

public:
	enum NodeType{NT_MASTER, NT_NORMAL};
	friend class session;

public:
	node(boost::asio::io_service& io_service, unsigned short port, NodeType nt)
		: nt_(nt)
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
		, is_requesting(false)
		, is_feedback(false)
		, is_receiving(false)
		, master_session(NULL)
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
	void Distribute(session* new_session, std::string ip);
	void ParseMetafile();
	void RequestFiles();
	void Work();
	void Feedback();

private:
	void start_accept();
	void start_scan();
	void start_ping();
	void handle_accept(session* new_session, const boost::system::error_code& error);
	void handle_accept_file(session* new_session, file_struct* file,
		const boost::system::error_code& error);
	void handle_connect(session* new_session, const boost::system::error_code& error);
	void handle_connect(session* new_session, msg_struct* msg,
		const boost::system::error_code& error);
	void handle_result(MsgType mt);
	void handle_msg(session* new_session, MyMsg msg);
	void send_metafile(session* new_session, addr_struct* addr,
		const boost::system::error_code& error);
	void send_file(session* new_session, file_struct* file,
		const boost::system::error_code& error);

private:
	NodeType nt_;
	std::string ip_;
	std::string master_ip;
	std::string metafile_name;
	unsigned short listen_port;
	boost::asio::io_service& io_service_;
	boost::asio::ip::tcp::acceptor acceptor_;
	boost::asio::ip::tcp::acceptor file_acceptor_;


	bool is_busy;
	bool is_connected;
	bool is_scan_finished;
	bool is_requesting;
	bool is_ping_busy;
	bool is_feedback;
	bool is_receiving;

	int limit_filenum_to_transfer;
	int cur_filenum;


	std::vector<node_struct> available_list;
	std::vector<task_struct> task_list_;
	std::vector<task_struct> request_list;
	std::vector<task_struct> feedback_list;

	session* master_session;

public:
	static std::fstream outfile;
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
		, is_recving(false)
	{
		is_available = true;
	}

	boost::asio::ip::tcp::socket& socket()
	{
		return socket_;
	}

	void recv_msg()
	{
		if (is_recving)
		{
			return;
		}
		log("Start receive");
		is_recving = true;
		msg_in.free();
		boost::asio::async_read(socket_,
			boost::asio::buffer(msg_in.data(), MyMsg::header_length),
			boost::bind(&session::handle_read_header, this,
			boost::asio::placeholders::error));
	}

	void send_msg(MsgType mt, const char* szbuf)
	{
		msg_out.free();
		bool write_in_progress = !msg_out_que_.empty();

		if (msg_out.encode_body(mt, szbuf))
		{
			msg_out.encode_header();
			msg_out_que_.push_back(msg_out);
			if (!write_in_progress)
			{
				boost::asio::async_write(socket_,
					boost::asio::buffer(msg_out_que_.front().data(),
					msg_out_que_.front().length()),
					boost::bind(&session::handle_write, this,
					boost::asio::placeholders::error));
			}
		}
		else
		{
			if (is_available)
			{
				delete this;
				is_available = false;
			}
		}
	}

	void send_file(std::string filename, unsigned __int64 filesize)
	{
		log("Send...");
		file.open(filename, std::ios::in|std::ios::binary);
		if (filesize > max_data_block)
		{
			file.read(data_buf, max_data_block);
			boost::asio::async_write(socket_,
				boost::asio::buffer(data_buf, max_data_block),
				boost::bind(&session::handle_write_file, this,
				filesize-max_data_block,
				boost::asio::placeholders::error));
		}
		else
		{
			file.read(data_buf, filesize);
			boost::asio::async_write(socket_,
				boost::asio::buffer(data_buf, static_cast<size_t>(filesize)),
				boost::bind(&session::handle_write_over, this,
				filesize,
				boost::asio::placeholders::error));
		}
	}

	void recv_file(std::string filename, unsigned __int64 filesize)
	{
		log("Recv...");
		file.open(filename, std::ios::out|std::ios::binary);
		if (filesize>max_data_block)
		{
			boost::asio::async_read(socket_,
				boost::asio::buffer(data_buf, max_data_block),
				boost::bind(&session::handle_read_file, this,
				filesize-max_data_block,
				boost::asio::placeholders::error));
		}
		else
		{
			boost::asio::async_read(socket_,
				boost::asio::buffer(data_buf, static_cast<size_t>(filesize)),
				boost::bind(&session::handle_read_over, this,
				filesize,
				boost::asio::placeholders::error));
		}
	}

private:
	void handle_read_header(const boost::system::error_code& error)
	{
		if (!error && msg_in.decode_header())
		{
			boost::asio::async_read(socket_,
				boost::asio::buffer(msg_in.body(), msg_in.body_length()),
				boost::bind(&session::handle_read_body, this,
				boost::asio::placeholders::error));
		}
		else
		{
			log(error.message().c_str());
			if (is_available)
			{
				is_available = false;
				delete this;
			}
		}
	}

	void handle_read_body(const boost::system::error_code& error)
	{
		if (!error)
		{
			log(msg_in.body());
			log("Read over");
			is_recving = false;
			owner_->handle_msg(this, msg_in);
		}
		else
		{
			log(error.message().c_str());
			if (is_available)
			{
				is_available = false;
				delete this;
			}
		}
	}

	void handle_read_file(unsigned __int64 bytes_to_transfer,
		const boost::system::error_code& error)
	{
		if (!error)
		{
			file.write(data_buf, max_data_block);
			if (bytes_to_transfer > max_data_block)
			{
				boost::asio::async_read(socket_,
					boost::asio::buffer(data_buf, max_data_block),
					boost::bind(&session::handle_read_file, this,
					bytes_to_transfer-max_data_block,
					boost::asio::placeholders::error));
			}
			else
			{
				boost::asio::async_read(socket_,
					boost::asio::buffer(data_buf, static_cast<size_t>(bytes_to_transfer)),
					boost::bind(&session::handle_read_over, this,
					bytes_to_transfer,
					boost::asio::placeholders::error));
			}
		}
		else
		{
			file.close();
			log(error.message().c_str());
			if (is_available)
			{
				is_available = false;
				delete this;
				if (st_ == ST_METAFILE)
				{
					owner_->handle_result(MT_METAFILE_FAIL);
				}
				else if ( st_ == ST_FILE)
				{
					owner_->handle_result(MT_FILE_FAIL);
				}
				else if (st_ == ST_FILE_BACK)
				{
					owner_->handle_result(MT_FILE_BACK_FAIL);
				}
			}
		}
	}

	void handle_write_file(unsigned __int64 bytes_to_transfer,
		const boost::system::error_code& error)
	{
		if (!error)
		{
			if (bytes_to_transfer > max_data_block)
			{
				file.read(data_buf, max_data_block);
				boost::asio::async_write(socket_,
					boost::asio::buffer(data_buf, max_data_block),
					boost::bind(&session::handle_write_file, this,
					bytes_to_transfer-max_data_block,
					boost::asio::placeholders::error));
			}
			else
			{
				file.read(data_buf, bytes_to_transfer);
				boost::asio::async_write(socket_,
					boost::asio::buffer(data_buf, static_cast<size_t>(bytes_to_transfer)),
					boost::bind(&session::handle_write_over, this,
					bytes_to_transfer,
					boost::asio::placeholders::error));
			}
		}
		else
		{
			file.close();
			log(error.message().c_str());
			if (is_available)
			{
				is_available = false;
				delete this;
				if (st_ == ST_FILE_BACK)
				{
					owner_->handle_result(MT_FILE_BACK_FAIL);
				}
			}
		}
	}

	void handle_write(const boost::system::error_code& error)
	{
		if (!error)
		{
			msg_out_que_.pop_front();
			recv_msg();
			if (!msg_out_que_.empty())
			{
				boost::asio::async_write(socket_,
					boost::asio::buffer(msg_out_que_.front().data(),
					msg_out_que_.front().length()),
					boost::bind(&session::handle_write, this,
					boost::asio::placeholders::error));
			}
		}
		else
		{
			log(error.message().c_str());
			if (is_available)
			{
				is_available = false;
				delete this;
			}
		}
	}

	void handle_read_over(unsigned __int64 last_length,
		const boost::system::error_code& error)
	{
		if (!error)
		{
			log("Recv Over");
			file.write(data_buf, last_length);
			file.close();
			if (st_ == ST_METAFILE)
			{
				owner_->handle_result(MT_METAFILE_FINISH);
			}
			else if (st_ == ST_FILE)
			{
				owner_->handle_result(MT_FILE_FINISH);
			}
			else if (st_ == ST_FILE_BACK)
			{
				owner_->handle_result(MT_FILE_BACK_FINISH);
			}
		}
		else
		{
			file.close();
			log(error.message().c_str());
			if (st_ == ST_METAFILE)
			{
				owner_->handle_result(MT_METAFILE_FAIL);
			}
			else if (st_ == ST_FILE)
			{
				owner_->handle_result(MT_FILE_FAIL);
			}
			else if (st_ == ST_FILE_BACK)
			{
				owner_->handle_result(MT_FILE_BACK_FAIL);
			}
		}
		if (is_available)
		{
			is_available = false;
			delete this;
		}
	}

	void handle_write_over(unsigned __int64 last_length,
		const boost::system::error_code& error)
	{
		if (!error)
		{
			log("Send Over");
			file.close();
			if (st_ == ST_FILE_BACK)
			{
				owner_->handle_result(MT_FILE_BACK_FINISH);
			}
			else if (st_ == ST_METAFILE)
			{
				owner_->handle_result(MT_METAFILE_FINISH);
			}
			else if (st_ == ST_FILE)
			{
				owner_->handle_result(MT_FILE_FINISH);
			}
		}
		else
		{
			file.close();
			log(error.message().c_str());
			if (st_ == ST_FILE_BACK)
			{
				owner_->handle_result(MT_FILE_BACK_FAIL);
			}
			else if (st_ == ST_METAFILE)
			{
				owner_->handle_result(MT_METAFILE_FAIL);
			}
			else if (st_ == ST_FILE)
			{
				owner_->handle_result(MT_FILE_FAIL);
			}
		}
		if (is_available)
		{
			is_available = false;
			delete this;
		}
	}

private:
	boost::asio::ip::tcp::socket socket_;
	MyMsg msg_out;
	MyMsg msg_in;
	enum{max_data_block = 102400};
	char data_buf[max_data_block];
	SessionType st_;
	node* owner_;
	bool is_recving;
	bool is_available;
	std::fstream file;
	std::deque<MyMsg> msg_out_que_;
};

