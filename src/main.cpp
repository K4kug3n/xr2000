#include <filesystem>
#include <fstream>
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
	// static constexpr uint32_t Magic = 0x5852324b;
	static constexpr uint8_t Magic[] = { 0x58, 0x52, 0x32, 0x4b };

	PacketType type;
	std::optional<uint8_t> request_id;
	std::vector<uint8_t> payload;

	Packet(PacketType type, std::optional<uint8_t> request_id, std::vector<uint8_t> payload = {})
		: type{ type }
		, request_id{ std::move(request_id) }
		, payload{ std::move(payload) } { }

	Packet(PacketType type, std::vector<uint8_t> payload = {})
		: type{ type }
		, request_id{ std::nullopt }
		, payload{ std::move(payload) } { }

	void pprint() const {
		std::cout << "Req ID present: " << std::boolalpha << request_id.has_value() << std::endl;
		if (request_id.has_value()) {
			std::cout << "Req ID: " << std::hex << *request_id << std::endl;
		}
		std::cout << "Type: " << type << " (0x" << std::hex << static_cast<int>(type) << ")" << std::endl;
		std::cout << "Payload length: " << std::dec << payload.size() << std::endl;
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

// Convert lenght field to length field length
uint8_t LF_to_LFL(uint8_t LF) {
	assert((LF <=4) && (LF != 3));
	if (LF == 4) {
		return 3;
	}

	return LF;
}

Packet recv_packet(TCPConnect& connection) {
	connection.bytes().clear();

	size_t nb_bytes = connection.recv();
	if (nb_bytes < 5) {
		throw std::runtime_error("Error: No minimum bytes read");
	}

	auto& bytes = connection.bytes();

	const uint8_t b = bytes[0];
	const uint8_t LFL = (0b11000000 & b) >> 6;
	const uint8_t request_id_present = (0b00100000 & b) >> 5;
	const uint8_t packet_type = (0b00011111 & b);
	assert(bytes[1] == Packet::Magic[0]);
	assert(bytes[2] == Packet::Magic[1]);
	assert(bytes[3] == Packet::Magic[2]);
	assert(bytes[4] == Packet::Magic[3]);

	size_t current_byte = 5;
	std::optional<uint8_t> request_id = std::nullopt;
	if (request_id_present > 0) {
		request_id = bytes[current_byte];
		current_byte += 1;
	}

	const uint8_t LF = LFL_to_LF(LFL);
	if (LF == 0) { // No payload
		return Packet{
			static_cast<PacketType>(packet_type),
			request_id
		};
	}

	uint32_t payload_length = 0;
	for(uint8_t i = 0; i < LF; ++i) {
		payload_length = ((static_cast<uint32_t>(bytes[current_byte]) & 0x000000FF) << (8 * i)) | payload_length;
		current_byte += 1;
	}

	std::vector<uint8_t> payload;
	payload.reserve(payload_length);
	for(int i = current_byte; i < nb_bytes; ++i) {
		payload.push_back(bytes[i]);
	}

	bytes.clear();
	while(payload.size() < payload_length) {
		nb_bytes = connection.recv();

		// TODO: Handle if more data received than expected
		for(size_t i = 0; i < bytes.size(); ++i) {
			payload.push_back(bytes[i]);
		}
		bytes.clear();
	}

	return Packet{
		static_cast<PacketType>(packet_type),
		std::move(request_id),
		std::move(payload)
	};
}

uint8_t compute_LF(uint32_t payload_size) {
	if ((payload_size & 0xFFFF0000) > 0)
		return 4;
	if ((payload_size & 0x0000FF00) > 0)
		return 2;
	if (payload_size > 0)
		return 1;
	return 0;
}

void send_packet(TCPConnect& connection, const Packet& p) {
	const uint32_t payload_size = p.payload.size();
	const uint8_t LF = compute_LF(payload_size);
	const uint8_t LFL = LF_to_LFL(LF);
	const bool request_id_present = p.request_id.has_value();

	const size_t packet_size = 1 + 4 + static_cast<size_t>(request_id_present) + LF + payload_size;
	std::vector<char> data;
	data.reserve(packet_size);

	// First byte: LFL + request id present + packet type
	data.push_back(
		(LFL << 6) | (static_cast<uint8_t>(request_id_present) << 5) | static_cast<uint8_t>(p.type)
	);

	// Add request id if present
	if (request_id_present) {
		data.push_back(p.request_id.value());
	}

	// Magic number
	data.push_back(Packet::Magic[0]);
	data.push_back(Packet::Magic[1]);
	data.push_back(Packet::Magic[2]);
	data.push_back(Packet::Magic[3]);

	// Payload length (little endian)
	for (size_t i = 0; i < LF; ++i) {
		data.push_back(
			static_cast<uint8_t>((payload_size >> (8*i)) & 0x000000FF)
		);
	}

	// Payload
	// TODO: replace by copy 
	for (size_t i = 0; i < payload_size; ++i) {
		data.push_back(p.payload[i]);
	}

	connection.send(data);
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
		p.payload.size()
	};
	std::cout << doc << std::endl;
}

struct CredentialInfos {
	std::vector<uint8_t> username;
	std::vector<uint8_t> password;

	void save_on_disk(std::string filepath) const {
		std::ofstream outfile{ filepath, std::ios::binary };
		if (!outfile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to save credential");
		}

		const uint8_t username_length = username.size();
		outfile.write(reinterpret_cast<const char*>(&username_length), sizeof(uint8_t));
		outfile.write(reinterpret_cast<const char*>(username.data()), username.size());

		const uint8_t password_length = password.size();
		outfile.write(reinterpret_cast<const char*>(&password_length), sizeof(uint8_t));
		outfile.write(reinterpret_cast<const char*>(password.data()), password.size());

		outfile.close();
	}

	void read_on_disk(std::string filepath) {
		std::ifstream infile{ filepath, std::ios::binary };
		if (!infile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to save credential");
		}

		uint8_t username_length;
		infile.read(reinterpret_cast<char*>(&username_length), sizeof(username_length));
		username.resize(username_length);
		infile.read(reinterpret_cast<char*>(username.data()), username_length);

		uint8_t password_length;
		infile.read(reinterpret_cast<char*>(&password_length), sizeof(password_length));
		password.resize(password_length);
		infile.read(reinterpret_cast<char*>(password.data()), password_length);

		infile.close();
	}

	void pprint() const {
		std::cout << "Username: 0x" << std::hex;
		for(auto v : username) {
			std::cout << v;
		}
		std::cout << std::dec << std::endl;

		std::cout << "Password: 0x" << std::hex;
		for(auto v : password) {
			std::cout << v;
		}
		std::cout << std::dec << std::endl;
	}
};

CredentialInfos handle_registered_packet(const Packet& p) {
	assert(p.type == PacketType::Registered);

	size_t offset = 0;
	const uint8_t username_length = p.payload[offset++];
	const std::vector<uint8_t> username(p.payload.begin() + offset, p.payload.begin() + offset + username_length);
	offset += username_length;

	const uint8_t password_length = p.payload[offset++];
	const std::vector<uint8_t> password(p.payload.begin() + offset, p.payload.begin() + offset + password_length);

	return CredentialInfos {
		username,
		password
	};
}

enum ResultType {
	Success,
	Error
};

std::ostream& operator<<(std::ostream& out, ResultType value) {
	#define PROCESS_VAL(p) case(p): out << #p; break;
	switch (value) {
		PROCESS_VAL(ResultType::Success);
		PROCESS_VAL(ResultType::Error);
	}
	#undef PROCESS_VAL

	return out;
}

struct Result {
	ResultType type;
	std::optional<uint8_t> error_type;

	void pprint() const {
		std::cout << "Result {" << std::endl;
		std::cout << "\t" << type << std::endl;
		if (error_type.has_value()) {
			std::cout << "\t0x" << std::hex << static_cast<int>(error_type.value()) << std::dec << std::endl;
		}
		std::cout << "}" << std::endl;
	}
};

Result handle_result_packet(const Packet& p) {
	assert(p.type == PacketType::Result);

	const ResultType type = static_cast<ResultType>(p.payload.size());
	const std::optional<uint8_t> error_type = (type == ResultType::Error)
	                                        ? std::make_optional(p.payload[0])
	                                        : std::nullopt;

	return Result {
		type,
		error_type
	 };
}

int main() {
	TCPConnect connection{ "clearsky.dev", "29438" };
	const Packet hello_packet = recv_packet(connection);
	handle_hello_packet(hello_packet);

	send_packet(connection, Packet{ PacketType::Help });
	const Packet doc_packet = recv_packet(connection);
	// handle_doc_packet(doc_packet);
	doc_packet.pprint();

	CredentialInfos credential;

	const std::string credential_file{ "credential.dat" };
	if (std::filesystem::exists(credential_file)) {
		std::cout << "Credential file detected" << std::endl;

		credential.read_on_disk(credential_file);
		
	} else {
		std::cout << "Credential file not detected" << std::endl;

		send_packet(connection, Packet{ PacketType::Register });
		const Packet register_packet = recv_packet(connection);
		switch (register_packet.type) {
			case PacketType::Registered:
				credential = handle_registered_packet(register_packet);
				break;
			case PacketType::Result : {
				const Result error = handle_result_packet(register_packet);
				error.pprint();
				throw std::runtime_error("Error: Result packet received during register");
				break;
			}
			default:
				register_packet.pprint();
				throw std::runtime_error("Error: Unexpected packet type during register");
		}

		 credential.save_on_disk(credential_file);
	}

	std::cout << "Credential:" << std::endl;
	credential.pprint();

	return 0;
}
