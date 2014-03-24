#include "node.h"
#include <iostream>
#include <algorithm>

using namespace boost::asio;
using boost::asio::ip::tcp;

void log(const char* p)
{
	time_t rawtime;
	time(&rawtime);
	char current_time[512] = "";

	strftime(current_time, sizeof(current_time),
		"[%Y/%m/%d %H:%M:%S] ", localtime(&rawtime));
	std::string out(current_time);
	out += p;
	std::cout<<out<<"\n";
}

bool node::Initialize()
{
	try
	{
		tcp::resolver resolver(io_service_);
		tcp::resolver::query query(ip::host_name(), "");
		tcp::resolver::iterator iter = resolver.resolve(query);
		tcp::resolver::iterator end;
		while(iter != end)
		{
			tcp::endpoint ep = *iter++;
			std::string ip = ep.address().to_string();
			int index = ip.find('.');
			if (index == std::string::npos)
			{
				continue;
			}
			else
			{
				std::cout<<"Confirm ip is"<<ip<<"?(Y/N)"<<"\n";
				char yesno;
				std::cin>>yesno;
				if (yesno == 'y' || yesno == 'Y')
				{
					ip_ = ip;
					return true;
				}
				continue;
			}
		}
	}
	catch (std::exception& e)
	{
		log(e.what());
		return false;
	}
	return false;
}

void node::ParseProj()
{

}

void node::Start()
{
	if (!is_connected)
	{
		return;
	}

	if (task_list_.empty())
	{
		ParseProj();
	}

	if (!is_scan_finished)
	{
		log("Last scan does not finished");
	}

	start_scan();

	//start_ping();
	while(true)
	{
		Sleep(1000);
	}
}

bool node::IsMaster()
{
	return (nt_ == NT_MASTER);
}

void node::start_accept()
{
	session* new_session = new session(io_service_, this);
	acceptor_.async_accept(new_session->socket(),
		boost::bind(&node::handle_accept, this, new_session,
		boost::asio::placeholders::error));
	log("Start accept");
}

void node::start_scan()
{
	log("Start scan...");
	is_scan_finished = false;

	session* new_session = NULL;

	int index = ip_.rfind('.')+1;
	std::string d = ip_.substr(index, ip_.length()-index);
	int nd = atoi(d.c_str());
	std::string abc = ip_.substr(0, index);
	char* newd = new char[10];

	for (int n = 1; n < 255; ++n)
	{
		new_session = new session(io_service_, this);

		memset(newd, 0, 10);
		int newnd = (nd+n)%255;
		_itoa_s(newnd, newd, 10, 10);
		std::string str_ip = abc+newd;

		new_session->socket().async_connect(
			tcp::endpoint(boost::asio::ip::address::from_string(str_ip),listen_port),
			boost::bind(&node::handle_connect, this, new_session,
			boost::asio::placeholders::error));
	}
	delete []newd;
	newd = NULL;
}

void node::start_ping()
{
	log("Start ping...");
	is_ping_busy = true;

	session* new_session = NULL;
	while(is_ping_busy)
	{
		while(!available_list.empty())
		{
			std::for_each(available_list.begin(), available_list.end(),
				[&](node_struct _node)
			{
				new_session = new session(io_service_, this);
				new_session->socket().async_connect(
					tcp::endpoint(boost::asio::ip::address::from_string(_node.ip_), listen_port),
					boost::bind(&node::handle_connect, this, new_session,
					new msg_struct(MT_PING, "", _node.ip_),
					boost::asio::placeholders::error));
			});
			Sleep(500);
		}
		Sleep(2000);
	}
}

void node::handle_accept(session* new_session,
	const boost::system::error_code& error)
{
	if (!error)
	{
		boost::system::error_code ec;
		tcp::endpoint ep = new_session->socket().remote_endpoint(ec);
		if (!ec)
		{
			log((ep.address().to_string()+" is connected").c_str());
		}
		else
		{
			log(ec.message().c_str());
		}
		new_session->handle_accept();
	}
	else
	{
		log(error.message().c_str());
		delete new_session;
	}
	start_accept();
}

void node::handle_connect(session* new_session,
	const boost::system::error_code& error)
{
	static int scan_count = 0;
	scan_count = (scan_count+1)%254;
	if (!error)
	{
		boost::system::error_code ec;
		tcp::endpoint ep = new_session->socket().remote_endpoint(ec);
		if (!ec)
		{
			std::string ip = ep.address().to_string();
			log(("Send MT_MASTER to "+ip).c_str());
			new_session->send_msg(MT_MASTER, "hello leaf");
		}
		else
		{
			log(error.message().c_str());
			delete new_session;
		}

	}
	else
	{
		//log(error.message().c_str());
		delete new_session;
	}
	if (scan_count == 0)
	{
		is_scan_finished = true;
	}
}

void node::handle_connect(session* new_session, msg_struct* msg,
	const boost::system::error_code& error)
{
	if (!error)
	{
		boost::system::error_code ec;
		tcp::endpoint ep = new_session->socket().remote_endpoint(ec);
		if (!ec)
		{
			std::string ip = ep.address().to_string();
			new_session->send_msg(msg->mt_, msg->msg_.c_str());
		}
		else
		{
			log(ec.message().c_str());
		}
		delete msg;
	}
	else
	{
		if (msg->mt_ == MT_PING)
		{
			auto ite = std::find(available_list.begin(),
				available_list.end(), node_struct(msg->ip_));
			if (ite != available_list.end())
			{
				available_list.erase(ite);
				log(("Lose the connecttion from"+msg->ip_).c_str());
			}
		}
		log(error.message().c_str());
		delete new_session;
		delete msg;
	}
}

void node::handle_msg(std::string ip, MyMsg msg)
{
	MsgType mt = msg.msg_type();
	std::string result = msg.decode_body();
	session* new_session = NULL;
	switch(mt)
	{
	case MT_MASTER:
		{
			if (master_ip == "")
			{
				master_ip = ip;
				log(("Connected to the master node "+master_ip).c_str());
				new_session = new session(io_service_, this);
				new_session->socket().async_connect(
					tcp::endpoint(boost::asio::ip::address::from_string(ip), listen_port),
					boost::bind(&node::handle_connect, this, new_session,
					new msg_struct(MT_AVAILABLE, "successful"),
					boost::asio::placeholders::error));
			}
			else
			{
				new_session = new session(io_service_, this);
				new_session->socket().async_connect(
					tcp::endpoint(boost::asio::ip::address::from_string(ip), listen_port),
					boost::bind(&node::handle_connect, this, new_session,
					new msg_struct(MT_OCCUPIED, master_ip),
					boost::asio::placeholders::error));
			}
			break;
		}
	case MT_AVAILABLE:
		{
			auto ite = std::find(available_list.begin(),
				available_list.end(), node_struct(ip));
			if (ite == available_list.end())
			{
				available_list.push_back(node_struct(ip));
				log(("Add leaf node "+ip).c_str());
			}
			
			break;
		}
	case MT_OCCUPIED:
		{
			break;
		}
	case MT_METAFILE:
		{
			break;
		}
	case MT_METAFILE_READY:
		{
			break;
		}
	case MT_METAFILE_FINISH:
		{
			break;
		}
	case MT_METAFILE_FAIL:
		{
			break;
		}
	case MT_FILE_REQUEST:
		{
			break;
		}
	case MT_FILE:
		{
			break;
		}
	case MT_FILE_READY:
		{
			break;
		}
	case MT_FILE_FINISH:
		{
			break;
		}
	case MT_FILE_FAIL:
		{
			break;
		}
	case MT_FILE_BACK:
		{
			break;
		}
	case MT_FILE_BACK_READY:
		{
			break;
		}
	case MT_FILE_BACK_FINISH:
		{
			break;
		}
	case MT_FILE_BACK_FAIL:
		{
			break;
		}
	case MT_PING:
		{
			new_session = new session(io_service_, this);
			new_session->socket().async_connect(
				tcp::endpoint(boost::asio::ip::address::from_string(ip), listen_port),
				boost::bind(&node::handle_connect, this, new_session,
				new msg_struct(MT_PING_BACK, is_busy?"-1":"1", ip),
				boost::asio::placeholders::error));
			break;
		}
	case MT_PING_BACK:
		{
			auto ite = std::find(available_list.begin(),
				available_list.end(), node_struct(ip));
			if (ite != available_list.end())
			{
				if (result == "-1")
				{
					ite->is_busy = true;
				}
				else if (result == "1")
				{
					ite->is_busy = false;
				}
			}
			break;
		}
	case MT_ERROR:
		{
			break;
		}
	}
}