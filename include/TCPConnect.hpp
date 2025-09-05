#ifndef TCPCONNECT_HPP
#define TCPCONNECT_HPP

#include <string>
#include <vector>

class TCPConnect {
public:
	TCPConnect(std::string server_adress, std::string port_str);

	void send(const std::vector<char>& data) const;

	int sock;
private:
	std::string m_server_adress;
	std::string m_port_str;
};

#endif
