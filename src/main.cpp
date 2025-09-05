#include "TCPConnect.hpp"

int main() {
	TCPConnect connection{ "clearsky.dev", "29438" };

	std::vector<char> data_to_send{ '\0', 'X', 'R', '2', 'K' };
	connection.send(data_to_send);
	
	return 0;
}
