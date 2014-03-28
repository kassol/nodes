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
	node::outfile.open("log.txt", std::ios::out|std::ios::app);
	node::outfile<<out<<"\n";
	node::outfile.close();
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
	//task_list_.push_back(task_struct("D:\\proj.txt", 0));
	task_list_.push_back(task_struct("D:\\proj1.txt", 0));
	task_list_.push_back(task_struct("D:\\proj2.txt", 0));
}

void node::Distribute(session* new_session, std::string ip)
{
	auto ite_task = task_list_.begin();
	while(ite_task != task_list_.end())
	{
		if (ite_task->state_ == 0)
		{
			ite_task->ip_ = ip;
			ite_task->state_ = 1;
			char filesize[50] = "";
			unsigned __int64 nfilesize = 
				boost::filesystem::file_size(ite_task->task_);
			sprintf(filesize, "%I64x", nfilesize);
			std::string filename = 
				ite_task->task_.substr(ite_task->task_.rfind('\\')+1,
				ite_task->task_.size()-ite_task->task_.rfind('\\')-1);
			std::string strmsg = filesize+std::string("|");
			strmsg += filename;
			new_session->send_msg(MT_METAFILE, strmsg.c_str());
			break;
		}
		++ite_task;
	}
}

void node::ParseMetafile()
{
	log("Parse metafile");
	std::fstream infile;
	infile.open(metafile_name, std::ios::in);
	if (!infile)
	{
		log("Open metafile failed");
		return;
	}
	std::string filename;
	while(!infile.eof())
	{
		infile>>filename;
		request_list.push_back(task_struct(filename, 0));
		log(("Add request file "+filename).c_str());
	}

	RequestFiles();
}

void node::RequestFiles()
{
	log("Start request file");
	auto ite_task = request_list.begin();
	while(ite_task != request_list.end())
	{
		while(is_requesting)
		{
			Sleep(1000);
		}
		if (ite_task->state_ == 2)
		{
			++ite_task;
			if (ite_task == request_list.end())
			{
				break;
			}
		}
		is_requesting = true;

		if (ite_task->state_ == 0)
		{
			ite_task->state_ = 1;
		}
		log(("Request file "+ite_task->task_).c_str());
		master_session->send_msg(MT_FILE_REQUEST, ite_task->task_.c_str());
	}

	while(!request_list.empty())
	{
		auto ite = request_list.begin();
		while(ite != request_list.end())
		{
			if (ite->state_ == 2)
			{
				ite = request_list.erase(ite);
				continue;
			}
			++ite;
		}
		Sleep(1000);
	}

	log("Finish request file, Start working");

	Work();
}

void node::Work()
{
	Sleep(5000);

	feedback_list.push_back(task_struct("12.aux", 0));
	log("12.aux");
	feedback_list.push_back(task_struct("12.tfw", 0));
	log("12.tfw");
	Feedback();
}

void node::Feedback()
{
	auto ite = feedback_list.begin();
	while(ite != feedback_list.end())
	{
		while(is_feedback)
		{
			Sleep(1000);
		}
		if (ite->state_ == 2)
		{
			++ite;
			if (ite == feedback_list.end())
			{
				break;
			}
		}

		is_feedback = true;

		if (ite->state_ == 0)
		{
			ite->state_ = 1;
		}

		std::string filename = ite->task_;
		unsigned __int64 nfilesize = boost::filesystem::file_size(filename);
		char filesize[50];
		sprintf(filesize, "%I64x", nfilesize);
		std::string strmsg = filesize+std::string("|");
		strmsg += filename;

		master_session->send_msg(MT_FILE_BACK, strmsg.c_str());
	}

	while(!feedback_list.empty())
	{
		ite = feedback_list.begin();
		while(ite != feedback_list.end())
		{
			if (ite->state_ == 2)
			{
				ite = feedback_list.erase(ite);
				continue;
			}
			++ite;
		}
		Sleep(1000);
	}

	master_session->send_msg(MT_FINISH, "finish");
	is_busy = false;
	log("Free now");
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

	if (!is_ping_busy)
	{
		boost::thread ping_thread(boost::bind(&node::start_ping, this));
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
	while(available_list.empty())
	{
		Sleep(1000);
	}

	is_ping_busy = true;

	session* new_session = NULL;
	while(is_ping_busy)
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

		if (available_list.empty())
		{
			break;
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
		new_session->recv_msg();
	}
	else
	{
		log(error.message().c_str());
		delete new_session;
	}
	start_accept();
}

void node::handle_accept_file(session* new_session, file_struct* file,
	const boost::system::error_code& error)
{
	if (!error)
	{
		log(("RECV "+file->filename_).c_str());
		new_session->recv_file(file->filename_, file->filesize_);
	}
	else
	{
		log(error.message().c_str());
		delete new_session;
	}
	delete file;
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


	switch(mt)
	{
	case MT_MASTER:
		{
			new_session->recv_msg();
			if (master_ip == "")
			{
				master_ip = ip;
				log(("Connected to the master node "+master_ip).c_str());
				master_session = new_session;
				new_session->send_msg(MT_AVAILABLE, "successful");
			}
			else
			{
				if (master_ip == ip)
				{
					master_session = new_session;
				}
				new_session->send_msg(MT_OCCUPIED, master_ip.c_str());
			}
			break;
		}
	case MT_AVAILABLE:
		{
			new_session->recv_msg();
			auto ite = std::find(available_list.begin(),
				available_list.end(), node_struct(ip));
			if (ite == available_list.end())
			{
				available_list.push_back(node_struct(ip));
				log(("Add leaf node "+ip).c_str());
			}
			Distribute(new_session, ip);
			break;
		}
	case MT_OCCUPIED:
		{
			new_session->recv_msg();
			if (std::string(result) == ip_)
			{
				auto ite = std::find(available_list.begin(),
					available_list.end(), node_struct(ip));
				if (ite == available_list.end())
				{
					available_list.push_back(node_struct(ip));
					log(("Add leaf node "+ip).c_str());
				}
				Distribute(new_session, ip);
			}
			else
			{
				log((ip+" is occupied").c_str());
			}
			break;
		}
	case MT_METAFILE:
		{
			new_session->recv_msg();
			is_busy = true;
			char* pathname;
			unsigned __int64 filesize = _strtoui64(result.c_str(), &pathname, 16);
			metafile_name =
				std::string(pathname).substr(1, std::string(pathname).size()-1);

			unsigned short file_port = 8999;
			boost::system::error_code ec;
			tcp::endpoint ep(tcp::v4(), file_port);
			file_acceptor_.open(ep.protocol());
			file_acceptor_.bind(ep, ec);
			if (ec)
			{
				log(ec.message().c_str());
				new_session->send_msg(MT_METAFILE_FAIL, "");
				break;
			}
			file_acceptor_.listen();
			log("Start listen port 8999");
			session* file_session = new session(io_service_, this, ST_METAFILE);
			file_acceptor_.async_accept(file_session->socket(),
				boost::bind(&node::handle_accept_file, this, file_session,
				new file_struct(metafile_name, filesize),
				boost::asio::placeholders::error));

			char* pfileport = new char[5];
			memset(pfileport, 0, 5);
			sprintf(pfileport, "%04x", file_port);
			new_session->send_msg(MT_METAFILE_READY, pfileport);
			break;
		}
	case MT_METAFILE_READY:
		{
			new_session->recv_msg();
			char* rest;
			unsigned short file_port =
				(unsigned short)strtoul(result.c_str(), &rest, 16);
			log("Connect to the file_port");
			session* file_session = new session(io_service_, this, ST_METAFILE);
			file_session->socket().async_connect(
				tcp::endpoint(boost::asio::ip::address::from_string(ip), file_port),
				boost::bind(&node::send_metafile, this, file_session,
				new addr_struct(ip, file_port),
				boost::asio::placeholders::error));
			break;
		}
	case MT_METAFILE_FINISH:
		{
			new_session->recv_msg();
			break;
		}
	case MT_METAFILE_FAIL:
		{
			new_session->recv_msg();
			break;
		}
	case MT_FILE_REQUEST:
		{
			new_session->recv_msg();
			std::string filename(result);
			log(filename.c_str());
			unsigned __int64 nfilesize = boost::filesystem::file_size(filename);
			char filesize[50];
			sprintf(filesize, "%I64x", nfilesize);
			std::string strmsg = filesize+std::string("|");
			strmsg += filename;
			new_session->send_msg(MT_FILE, strmsg.c_str());
			break;
		}
	case MT_FILE:
		{
			new_session->recv_msg();
			char* pathname;
			unsigned __int64 filesize = _strtoui64(result.c_str(), &pathname, 16);
			std::string filepath =
				std::string(pathname).substr(1, std::string(pathname).size()-1);
			std::string filename = filepath.substr(filepath.rfind("\\")+1,
				filepath.size()-filepath.rfind("\\")-1);
			unsigned short file_port = 8999;
			boost::system::error_code ec;
			tcp::endpoint ep(tcp::v4(), file_port);
			file_acceptor_.open(ep.protocol());
			file_acceptor_.bind(ep, ec);
			if (ec)
			{
				log(ec.message().c_str());
				break;
			}
			file_acceptor_.listen();
			log("Start listen 8999");
			session* file_session = new session(io_service_, this, ST_FILE);
			file_acceptor_.async_accept(file_session->socket(),
				boost::bind(&node::handle_accept_file, this, file_session,
				new file_struct(filename, filesize),
				boost::asio::placeholders::error));

			char* pfileport = new char[5];
			memset(pfileport, 0, 5);
			sprintf(pfileport, "%04x", file_port);
			std::string strmsg = pfileport+std::string("|");
			strmsg += filepath;
			new_session->send_msg(MT_FILE_READY, strmsg.c_str());
			break;
		}
	case MT_FILE_READY:
		{
			new_session->recv_msg();
			char* rest;
			unsigned short file_port =
				(unsigned short)strtoul(result.c_str(), &rest, 16);
			std::string pathname =
				std::string(rest).substr(1, std::string(rest).size()-1);
			session* file_session = new session(io_service_, this, ST_FILE);
			file_session->socket().async_connect(
				tcp::endpoint(boost::asio::ip::address::from_string(ip), file_port),
				boost::bind(&node::send_file, this, file_session,
				new file_struct(pathname, 0),
				boost::asio::placeholders::error));
			break;
		}
	case MT_FILE_FINISH:
		{
			new_session->recv_msg();
			break;
		}
	case MT_FILE_FAIL:
		{
			new_session->recv_msg();
			break;
		}
	case MT_FILE_BACK:
		{
			new_session->recv_msg();
			char* pathname;
			unsigned __int64 filesize = _strtoui64(result.c_str(), &pathname, 16);
			std::string filepath =
				std::string(pathname).substr(1, std::string(pathname).size()-1);
			std::string filename = filepath.substr(filepath.rfind("\\")+1,
				filepath.size()-filepath.rfind("\\")-1);
			unsigned short file_port = 8999;
			boost::system::error_code ec;
			tcp::endpoint ep(tcp::v4(), file_port);
			file_acceptor_.open(ep.protocol());
			file_acceptor_.bind(ep, ec);
			if (ec)
			{
				log(ec.message().c_str());
				break;
			}
			file_acceptor_.listen();
			log("Start listen 8999");
			session* file_session = new session(io_service_, this, ST_FILE_BACK);
			file_acceptor_.async_accept(file_session->socket(),
				boost::bind(&node::handle_accept_file, this, file_session,
				new file_struct(filename, filesize),
				boost::asio::placeholders::error));

			char* pfileport = new char[5];
			memset(pfileport, 0, 5);
			sprintf(pfileport, "%04x", file_port);
			std::string strmsg = pfileport+std::string("|");
			strmsg += filepath;
			new_session->send_msg(MT_FILE_BACK_READY, strmsg.c_str());
			break;
		}
	case MT_FILE_BACK_READY:
		{
			new_session->recv_msg();
			char* rest;
			unsigned short file_port =
				(unsigned short)strtoul(result.c_str(), &rest, 16);
			std::string pathname =
				std::string(rest).substr(1, std::string(rest).size()-1);
			session* file_session = new session(io_service_, this, ST_FILE_BACK);
			file_session->socket().async_connect(
				tcp::endpoint(boost::asio::ip::address::from_string(ip), file_port),
				boost::bind(&node::send_file, this, file_session,
				new file_struct(pathname, 0),
				boost::asio::placeholders::error));
			break;
		}
	case MT_FILE_BACK_FINISH:
		{
			new_session->recv_msg();
			log("file back finish");
			break;
		}
	case MT_FILE_BACK_FAIL:
		{
			new_session->recv_msg();
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
			delete new_session;
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
	case MT_FINISH:
		{
			new_session->recv_msg();
			log((ip+" finish work").c_str());

			auto ite_task = task_list_.begin();
			while(ite_task != task_list_.end())
			{
				if (ite_task->state_ == 1 && ite_task->ip_ == ip)
				{
					ite_task->state_ = 2;
					break;
				}
				++ite_task;
			}

			Distribute(new_session, ip);
			break;
		}
	case MT_ERROR:
		{
			delete new_session;
			log("Error message");
			break;
		}
	}
}

void node::handle_result(MsgType mt)
{
	if (!IsMaster())
	{
		master_session->send_msg(mt, "");
		if (mt == MT_METAFILE_FINISH)
		{
			log("Stop listen on 8999");
			file_acceptor_.close();
			boost::thread thr(boost::bind(&node::ParseMetafile, this));
		}
		else if (mt == MT_FILE_FINISH)
		{
			log("Stop listen on 8999");
			file_acceptor_.close();
			auto ite = request_list.begin();
			while(ite != request_list.end())
			{
				if (ite->state_ == 1)
				{
					ite->state_ = 2;
					is_requesting = false;
					break;
				}
				++ite;
			}
			is_requesting = false;
		}
		else if (mt == MT_FILE_BACK_FINISH)
		{
			auto ite = feedback_list.begin();
			while(ite != feedback_list.end())
			{
				if (ite->state_ == 1)
				{
					ite->state_ = 2;
					is_feedback = false;
					break;
				}
				++ite;
			}
			is_feedback = false;
		}
		else if (mt == MT_FILE_BACK_FAIL)
		{
			is_feedback = false;
		}
	}
	else
	{
		if (mt == MT_FILE_BACK_FINISH)
		{
			log("Stop listen on 8999");
			file_acceptor_.close();
		}
		else if (mt == MT_FILE_BACK_FAIL)
		{
			log("Stop listen on 8999");
			file_acceptor_.close();
		}
	}
}

void node::send_metafile(session* new_session, addr_struct* addr,
	const boost::system::error_code& error)
{
	if (!error)
	{
		std::string task_path;
		for (size_t i = 0; i < task_list_.size(); ++i)
		{
			if (task_list_.at(i).ip_ == addr->ip_ && task_list_.at(i).state_ == 1)
			{
				task_path = task_list_.at(i).task_;
				break;
			}
		}
		
		if (task_path == "")
		{
			delete new_session;
			delete addr;
			return;
		}
		log("Send metafile");
		new_session->send_file(task_path, boost::filesystem::file_size(task_path));
	}
	else
	{
		log(error.message().c_str());
		delete new_session;
	}
	delete addr;
}

void node::send_file(session* new_session, file_struct* file,
	const boost::system::error_code& error)
{
	if (!error)
	{
		new_session->send_file(file->filename_,
			boost::filesystem::file_size(file->filename_));
	}
	else
	{
		log(error.message().c_str());
		delete new_session;
	}
	delete file;
}
