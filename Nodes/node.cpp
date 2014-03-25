#include "node.h"
#include <iostream>
#include <algorithm>

using namespace boost::asio;
using boost::asio::ip::tcp;

std::fstream node::outfile;

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
	node::outfile<<out<<"\n";
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
		new_session->recv_msg();
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

void node::handle_msg(session* new_session, MyMsg msg)
{
	boost::system::error_code ec;
	boost::asio::ip::tcp::endpoint ep = new_session->socket().remote_endpoint(ec);
	if (ec)
	{
		log(ec.message().c_str());
		delete new_session;
		return;
	}
	std::string ip = ep.address().to_string();
	MsgType mt = msg.msg_type();
	std::string result = msg.decode_body();

	new_session->recv_msg();

	switch(mt)
	{
	case MT_MASTER:
		{
			if (master_ip == "")
			{
				master_ip = ip;
				log(("Connected to the master node "+master_ip).c_str());

				new_session->send_msg(MT_AVAILABLE, "successful");
			}
			else
			{
				new_session->send_msg(MT_OCCUPIED, master_ip.c_str());
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
				log("Ping");
				session* ping_session = new session(io_service_, this);
				ping_session->socket().async_connect(
					tcp::endpoint(boost::asio::ip::address::from_string(ip), listen_port),
					boost::bind(&node::handle_connect, this, ping_session,
					new msg_struct(MT_PING, "", ip),
					boost::asio::placeholders::error));
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
			log("Ping back");
			new_session->send_msg(MT_PING_BACK, is_busy?"-1":"1");
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
			Sleep(2000);
			log("Ping");
			new_session->send_msg(MT_PING, "");
			break;
		}
	case MT_ERROR:
		{
			break;
		}
	}
}