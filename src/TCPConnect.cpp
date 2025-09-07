#include "TCPConnect.hpp"

#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <cstring>

#include <iostream>

TCPConnect::TCPConnect(std::string server_adress, std::string port_str)
	: sock{ -1 }
	, m_server_adress{ std::move(server_adress) }
	, m_port_str{ std::move(port_str) }
{
	struct addrinfo hints, *server_info, *p;

	std::memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	int status = getaddrinfo(m_server_adress.c_str(), m_port_str.c_str(), &hints, &server_info);
	if (status != 0) {
		throw std::runtime_error("Error: getaddrinfo failed: " + std::string{gai_strerror(status)} );
	}
	
	for(p = server_info; p != NULL; p = p->ai_next) {
		sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (sock < 0) {
			continue;
		}
	
		if (connect(sock, p->ai_addr, p->ai_addrlen) < 0) {
			close(sock);
			sock = -1;
			continue;
		}
	
		break;
	}
	
	if (sock == -1) {
		freeaddrinfo(server_info);
		throw std::runtime_error("Error: Failed to connect to any resolved adress");
	}
	
	std::cout << "Successfully connected to " << m_server_adress << ":" << m_port_str << std::endl;
	freeaddrinfo(server_info);
}

std::vector<uint8_t>& TCPConnect::bytes() {
	return m_pending_bytes;
}

void TCPConnect::send(const std::vector<char>& data) const {
	int bytes_send = ::send(sock, data.data(), data.size(), 0);
	if (bytes_send < 0) {
		throw std::runtime_error("Error: Failed to send data to server");
	}
}

size_t TCPConnect::recv() {
	char buff[2048];
	int bytes = ::recv(sock, buff, 2048, 0);
	if (bytes <= 0) {
		throw std::runtime_error("Error: Could not receive from the server");
	}

	for(int i = 0; i < bytes; ++i) {
		m_pending_bytes.push_back(buff[i]);
	}

	return static_cast<size_t>(bytes);
}
