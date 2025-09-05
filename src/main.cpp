#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>

#include <string_view>
#include <optional>
#include <vector>
#include <cassert>
#include <string>

#include "TCPConnect.hpp"

enum class PacketType {
	Help          = 0x00,
	Hello         = 0x01,
	Documentation = 0x02,
	Register      = 0x03,
	Registered    = 0x04,
	Login         = 0x05,
	GetStatus     = 0x07,
	Status        = 0x08,
	GetMail       = 0x09,
	Mail          = 0x0a,
	SendMail      = 0x0b,
	Configure     = 0x12,
	Route         = 0x14,
	Translate     = 0x15,
	Translation   = 0x16,
	Result        = 0x1f,
};

std::ostream& operator<<(std::ostream& out, PacketType value) {
	#define PROCESS_VAL(p) case(p): out << #p; break;
	switch (value) {
		PROCESS_VAL(PacketType::Help);
		PROCESS_VAL(PacketType::Hello);
		PROCESS_VAL(PacketType::Documentation);
		PROCESS_VAL(PacketType::Register);
		PROCESS_VAL(PacketType::Registered);
		PROCESS_VAL(PacketType::Login);
		PROCESS_VAL(PacketType::GetStatus);
		PROCESS_VAL(PacketType::Status);
		PROCESS_VAL(PacketType::GetMail);
		PROCESS_VAL(PacketType::Mail);
		PROCESS_VAL(PacketType::SendMail);
		PROCESS_VAL(PacketType::Configure);
		PROCESS_VAL(PacketType::Route);
		PROCESS_VAL(PacketType::Translate);
		PROCESS_VAL(PacketType::Translation);
		PROCESS_VAL(PacketType::Result);
	}
	#undef PROCESS_VAL

	return out;
}

struct Packet {
	uint8_t LFL; // 2 bits
	PacketType type;
	std::optional<uint8_t> request_id;
	uint32_t magic;
	uint32_t payload_length; // 0, 1, 2 or 4 bytes
	std::vector<uint8_t> payload;

	void pprint() const {
		std::cout << "LFL: " << std::hex << static_cast<int>(LFL) << std::endl;
		std::cout << "Req ID present: " << std::boolalpha << request_id.has_value() << std::endl;
		if (request_id.has_value()) {
			std::cout << "Req ID: " << std::hex << *request_id << std::endl;
		}
		std::cout << "Type: " << type << " (0x" << std::hex << static_cast<int>(type) << ")" << std::endl;
		std::cout << "Magic: "
				  << static_cast<uint8_t>((magic & 0xFF000000) >> 24)
				  << static_cast<uint8_t>((magic & 0x00FF0000) >> 16)
				  << static_cast<uint8_t>((magic & 0x0000FF00) >> 8)
				  << static_cast<uint8_t>(magic & 0x000000FF)
				  << std::endl;
		std::cout << "Payload length: " << std::dec << payload_length << std::endl;
	}
};

// Convert lenght field length to actual length field
uint8_t LFL_to_LF(uint8_t LFL) {
	assert(LFL < 4);
	if (LFL == 3) {
		return 4;
	}

	return LFL;
}

Packet recv_packet(TCPConnect& connection) {
	char buff[2048];
	int bytes = recv(connection.sock, buff, 2048, 0);
	// std::cout << "Read " << bytes << " bytes" << std::endl;

	if (bytes < 5) {
		throw std::runtime_error("Error: No minimum bytes read");
	}

	const char b = buff[0];
	const uint8_t LFL = (0b11000000 & b) >> 6;
	const uint8_t request_id_present = (0b00100000 & b) >> 5;
	const uint8_t packet_type = (0b00011111 & b);
	const uint32_t magic = static_cast<uint32_t>(buff[1]) << 24
				   | static_cast<uint32_t>(buff[2]) << 16
				   | static_cast<uint32_t>(buff[3]) << 8
				   | static_cast<uint32_t>(buff[4]);
	assert(magic == 0x5852324b);

	size_t current_byte = 5;
	std::optional<uint8_t> request_id = std::nullopt;
	if (request_id_present > 0) {
		request_id = buff[current_byte];
		current_byte += 1;
	}

	const uint8_t LF = LFL_to_LF(LFL);
	if (LF == 0) { // No payload
		return Packet{
			LFL,
			static_cast<PacketType>(packet_type),
			request_id,
			magic,
			0, {}
		};
	}

	uint32_t payload_length = 0;
	for(uint8_t i = 0; i < LF; ++i) {
		payload_length = ((static_cast<uint32_t>(buff[current_byte]) & 0x000000FF) << (8 * i)) | payload_length;
		current_byte += 1;
	}

	std::vector<uint8_t> payload;
	payload.reserve(payload_length);
	for(int i = current_byte; i < bytes; ++i) {
		payload.push_back(buff[i]);
	}

	while(payload.size() < payload_length) {
		bytes = recv(connection.sock, buff, 2048, 0);

		// TODO: Handle if more data received than expected
		for(size_t i = 0; i < bytes; ++i) {
			payload.push_back(buff[i]);
		}
	}

	return Packet{
		LFL,
		static_cast<PacketType>(packet_type),
		request_id,
		magic,
		payload_length,
		payload
	};
}

void handle_hello_packet(const Packet& p) {
	assert(p.type == PacketType::Hello);

	size_t offset = 0;
	const uint8_t protocol_version = p.payload[offset++];
	const uint8_t hostname_length = p.payload[offset++];
	const std::string hostname(p.payload.begin() + offset, p.payload.begin() + hostname_length + offset);
	offset += hostname_length;
	const uint8_t instr_length = p.payload[offset++];
	const std::string instr(p.payload.begin() + offset, p.payload.begin() + instr_length + offset);

	std::cout << "Protocol version: " << static_cast<int>(protocol_version) << std::endl;
	std::cout << "Hostname: " << hostname << std::endl;
	std::cout << "Instruction: " << instr << std::endl;
}

void handle_doc_packet(const Packet& p) {
	assert(p.type == PacketType::Documentation);

	const std::string_view doc{
		reinterpret_cast<const char*>(p.payload.data()),
		static_cast<size_t>(p.payload_length)
	};
	std::cout << doc << std::endl;
}

int main() {
	TCPConnect connection{ "clearsky.dev", "29438" };
	const Packet hello_packet = recv_packet(connection);
	handle_hello_packet(hello_packet);

	std::vector<char> data_to_send{ '\0', 'X', 'R', '2', 'K' };
	connection.send(data_to_send);

	const Packet doc_packet = recv_packet(connection);
	handle_doc_packet(doc_packet);
	doc_packet.pprint();

	return 0;
}
