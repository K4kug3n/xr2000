#ifndef TCPCONNECT_HPP
#define TCPCONNECT_HPP

#include <string>
#include <vector>
#include <cstdint>

class TCPConnect {
public:
	TCPConnect(std::string server_adress, std::string port_str);

	std::vector<uint8_t>& bytes();

	size_t recv();
	void send(const std::vector<char>& data) const;

private:
	int sock;
	std::string m_server_adress;
	std::string m_port_str;

	std::vector<uint8_t> m_pending_bytes;
};

#endif
