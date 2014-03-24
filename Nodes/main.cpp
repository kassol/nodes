#define _CRT_SECURE_NO_WARNINGS

#include <iostream>

#include "node.h"

void run_service(boost::asio::io_service& service)
{
	service.run();
}

int main(int argc, char* argv[])
{
	boost::asio::io_service service;
	unsigned short nport = 8992;
	node* nodeptr = new node(service, nport);

	if (argc == 2)
	{
		std::string arg(argv[1]);
		if (arg == "server")
		{
			boost::thread thrd(boost::bind(run_service, boost::ref(service)));
			Sleep(1000);
			nodeptr->Start();
		}
		else if (arg == "client")
		{
			service.run();
		}
	}
	delete nodeptr;
	nodeptr = NULL;
	return 0;
}

