#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

#include <string_view>
#include <optional>
#include <vector>
#include <cassert>
#include <string>

#include <chrono>
#include <thread>

#include "TCPConnect.hpp"
#include "StringProcess.hpp"

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

template <typename T>
T pop_and_get(std::queue<T>& collection) {
	const T value = collection.front();
	collection.pop();

	return value;
}

Packet recv_packet(TCPConnect& connection) {
	size_t nb_bytes = connection.recv();
	if (nb_bytes < 5) {
		throw std::runtime_error("Error: No minimum bytes read");
	}

	auto& bytes = connection.bytes();

	const uint8_t b = pop_and_get(bytes);
	const uint8_t LFL = (0b11000000 & b) >> 6;
	const uint8_t request_id_present = (0b00100000 & b) >> 5;
	const uint8_t packet_type = (0b00011111 & b);
	for (size_t i = 0; i < 4; ++i) {
		assert(pop_and_get(bytes) == Packet::Magic[i]);
	}

	std::optional<uint8_t> request_id = std::nullopt;
	if (request_id_present > 0) {
		request_id = pop_and_get(bytes);
	}

	const uint8_t LF = LFL_to_LF(LFL);
	if (LF == 0) { // No payload
		return Packet{
			static_cast<PacketType>(packet_type),
			request_id
		};
	}

	uint32_t payload_length = 0;
	// Read payload length (little endian)
	for(uint8_t i = 0; i < LF; ++i) {
		payload_length = ((static_cast<uint32_t>(pop_and_get(bytes)) & 0x000000FF) << (8 * i)) | payload_length;
	}

	std::vector<uint8_t> payload;
	payload.reserve(payload_length);
	while(bytes.size() < payload_length) {
		connection.recv();
	}

	// TODO: Do a copy
	for (size_t i = 0; i < payload_length; ++i) {
		payload.push_back(pop_and_get(bytes));
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
			std::runtime_error("Error: Could not open " + filepath + " to read credential");
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
		std::cout << "Credential:" << std::endl;
		std::cout << "\tUsername: 0x" << std::hex;
		for(auto v : username) {
			std::cout << v;
		}
		std::cout << std::dec << std::endl;

		std::cout << "\tPassword: 0x" << std::hex;
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

struct Result {
	uint8_t code;

	bool success() const {
		return code == 0x00;
	}

	bool error() const {
		return !success();
	}

	std::string to_string() const {
		switch (code) {
			case 0x00: return "Success";
			case 0x01: return "Already authenticated";
			case 0x02: return "Not autheticated";
			case 0x03: return "Invalid credential";
			case 0x04: return "Not authorized for tranceive";
			case 0x11: return "Registration rate limit";
			case 0x12: return "Translation limiting";
			case 0x20: return "Tranceiver malfunction";
			case 0x21: return "Invalid config";
			case 0x40: return "Mail not found";
			case 0x50: return "Translation not found";
			default: return "Unknow result code";
		}
	}

	void pprint() const {
		std::cout << "Result:" << std::endl;
		std::cout << "\t" << to_string() << " (0x" << std::hex << static_cast<int>(code) << std::dec << ")" << std::endl;
	}
};

Result handle_result_packet(const Packet& p) {
	assert(p.type == PacketType::Result);
	assert(p.payload.size() == 1);

	const uint8_t code = p.payload[0];

	return Result{ code };
}

Packet write_login_packet(const CredentialInfos& credential) {
	const uint8_t username_length = static_cast<uint8_t>(credential.username.size());
	const uint8_t password_length = static_cast<uint8_t>(credential.password.size());

	std::vector<uint8_t> payload;
	payload.reserve(username_length + password_length + 2);

	// TODO: Use copy
	payload.push_back(username_length);
	for (auto v : credential.username) {
		payload.push_back(v);
	}
	payload.push_back(password_length);
	for (auto v : credential.password) {
		payload.push_back(v);
	}

	return Packet { PacketType::Login, payload };
}

struct Status {
	std::optional<uint32_t> nb_mails;
	uint32_t connection_time;
	bool authenticated;
	bool authorized; // Tranceiver usage
	bool configured;

	void pprint() const {
		std::cout << "Status:" << std::endl;
		std::cout << "\tConnected since " << connection_time << "s" << std::endl;
		std::cout << "\tAuthenticated: " << std::boolalpha << authenticated << std::dec << std::endl;
		std::cout << "\tAuthorized: " << std::boolalpha << authorized << std::dec << std::endl;
		std::cout << "\tConfigured: " << std::boolalpha << configured << std::dec << std::endl;
		if (nb_mails.has_value()) {
			std::cout << "\tNb mails: " << nb_mails.value() << std::endl;
		}
	}
};

Status handle_status_packet(const Packet& p) {
	assert(p.type == PacketType::Status);
	assert(p.payload.size() == 9);

	const uint32_t nb_mails_v = ((static_cast<uint32_t>(p.payload[3]) & 0x000000FF) << 24)
	                          | ((static_cast<uint32_t>(p.payload[2]) & 0x000000FF) << 16)
	                          | ((static_cast<uint32_t>(p.payload[1]) & 0x000000FF) << 8)
	                          | (static_cast<uint32_t>(p.payload[0]) & 0x000000FF);
	const std::optional<uint32_t> nb_mail = (nb_mails_v == 0xffffffff)
	                                      ? std::nullopt
	                                      : std::make_optional(nb_mails_v);

	const uint32_t connection_time = ((static_cast<uint32_t>(p.payload[7]) & 0x000000FF) << 24)
	                               | ((static_cast<uint32_t>(p.payload[6]) & 0x000000FF) << 16)
	                               | ((static_cast<uint32_t>(p.payload[5]) & 0x000000FF) << 8)
	                               | (static_cast<uint32_t>(p.payload[4]) & 0x000000FF);

	const bool authenticated = !(p.payload[8] & 0b00000100);
	const bool authorized = !(p.payload[8] & 0b00000010);
	const bool configured = !(p.payload[8] & 0b00000001);

	return Status {
		nb_mail,
		connection_time,
		authenticated,
		authorized,
		configured
	};
}

struct Configuration {
	uint32_t frequency;
	uint32_t baudrate;
	uint8_t modulation;

	// Prevent spoil
	void read_on_disk(const std::string& filepath) {
		std::ifstream infile{ filepath };
		if (!infile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to read configuration");
		}

		// Prevent reading value as char
		uint16_t temp_modulation = 0;
		infile >> frequency;
		infile >> baudrate;
		infile >> temp_modulation;

		modulation = static_cast<uint8_t>(temp_modulation);

		infile.close();
	}

	void pprint() {
		std::cout << "Configuration: " << std::endl;
		std::cout << "\tFrequency: " << frequency << std::endl;
		std::cout << "\tBaudrate: " << baudrate << std::endl;
		std::cout << "\tModulation: ";
		switch (modulation) {
			case 0x00: std::cout << "AM"; break;
			case 0x01: std::cout << "FM"; break;
			case 0x02: std::cout << "PM"; break;
			case 0x03: std::cout << "BPSK"; break;
			default: std::cout << "Unknown (0x" << std::hex << static_cast<int>(modulation) << std::dec << ")";
		}
		std::cout << std::endl;
	}
};

Packet write_configuration_packet(const Configuration& config) {
	std::vector<uint8_t> payload;
	payload.reserve(9);

	for (size_t i = 0; i < 4; ++i) {
		payload.push_back(
			static_cast<uint8_t>((config.frequency >> (8*i)) & 0x000000FF)
		);
	}

	for (size_t i = 0; i < 4; ++i) {
		payload.push_back(
			static_cast<uint8_t>((config.baudrate >> (8*i)) & 0x000000FF)
		);
	}

	payload.push_back(config.modulation);

	return Packet { PacketType::Configure, payload };
}

Packet write_translate_packet(const std::string& word) {
	const std::vector<uint8_t> payload(word.begin(), word.end());

	return Packet{ PacketType::Translate, payload };
}

std::string handle_translation_packet(const Packet& p) {
	assert(p.type == PacketType::Translation);
	return std::string(p.payload.begin(), p.payload.end());
}

struct Dictionnary {
	std::unordered_map<std::string, std::string> mapping;

	bool contains(const std::string& w) const {
		return mapping.contains(w);
	}

	const std::string& operator[](const std::string& w) const {
		return mapping.at(w);
	}

	std::string& operator[](const std::string& w) {
		return mapping[w];
	}

	size_t size() const {
		return mapping.size();
	}

	void save_on_disk(std::string filepath) const {
		std::ofstream outfile{ filepath, std::ios::binary };
		if (!outfile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to save dictionnary");
		}

		for(const auto& pair : mapping) {
			outfile << pair.first << " " << pair.second << "\n";
		}

		outfile.close();
	}

	void read_on_disk(std::string filepath) {
		std::ifstream infile{ filepath, std::ios::binary };
		if (!infile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to read dictionnary");
		}

		std::string key, value;
		while (infile >> key >> value) {
			mapping[key] = value;
		}

		infile.close();
	}
};

Packet write_getmail_packet(uint32_t mail_id) {
	std::vector<uint8_t> payload;
	payload.reserve(4);

	for (size_t i = 0; i < 4; ++i) {
		payload.push_back(
			static_cast<uint8_t>((mail_id >> (8*i)) & 0x000000FF)
		);
	}

	return Packet { PacketType::GetMail, payload };
}

struct Mail {
	uint32_t id;
	uint32_t timestamp;
	std::string sender_username;
	std::string content;

	void pprint() const {
		std::cout << "Mail n° " << id << std::endl;
		std::cout << "\tSent by " << sender_username << " at " << timestamp << std::endl;
		std::cout << "\tContent: " << content << std::endl;
	}

	void save_on_disk(std::string filepath) const {
		std::ofstream outfile{ filepath, std::ios::out };
		if (!outfile.is_open()) {
			std::runtime_error("Error: Could not open " + filepath + " to save mail");
		}

		outfile << "Mail n°" << id << "\n";
		outfile << "Sent by " << sender_username << " at " << timestamp << "\n";
		outfile << "Content:" << "\n";
		outfile << content;

		outfile.close();
	}

	void translate(const Dictionnary& dict) {
		content = ::translate(content, dict.mapping);
	}
};

Mail handle_mail_packet(const Packet& p) {
	assert(p.type == PacketType::Mail);

	const uint32_t id = ((static_cast<uint32_t>(p.payload[3]) & 0x000000FF) << 24)
	                  | ((static_cast<uint32_t>(p.payload[2]) & 0x000000FF) << 16)
	                  | ((static_cast<uint32_t>(p.payload[1]) & 0x000000FF) << 8)
	                  | (static_cast<uint32_t>(p.payload[0]) & 0x000000FF);
	
	const uint32_t timestamp = ((static_cast<uint32_t>(p.payload[7]) & 0x000000FF) << 24)
	                         | ((static_cast<uint32_t>(p.payload[6]) & 0x000000FF) << 16)
	                         | ((static_cast<uint32_t>(p.payload[5]) & 0x000000FF) << 8)
	                         | (static_cast<uint32_t>(p.payload[4]) & 0x000000FF);

	size_t offset = 8;
	const uint8_t username_length = p.payload[offset++];
	// TODO: Use copy
	std::vector<uint8_t> username;
	for (size_t i = 0; i < username_length; ++i) {
		username.push_back(p.payload[offset++]);
	}
	
	uint32_t content_length = 0;
	// Read payload length (little endian)
	for(uint8_t i = 0; i < 4; ++i) {
		content_length = ((static_cast<uint32_t>(p.payload[offset++]) & 0x000000FF) << (8 * i)) | content_length;
	}
	// TODO: Use copy
	std::vector<uint8_t> content;
	for (size_t i = 0; i < content_length; ++i) {
		content.push_back(p.payload[offset++]);
	}

	return Mail {
		id, timestamp,
		std::string{ username.begin(), username.end() },
		std::string{ content.begin(), content.end() }
	};
}

int main() {
	TCPConnect connection{ "clearsky.dev", "29438" };
	const Packet hello_packet = recv_packet(connection);
	handle_hello_packet(hello_packet);
	connection.clear_bytes();

	send_packet(connection, Packet{ PacketType::Help });
	const Packet doc_packet = recv_packet(connection);
	// handle_doc_packet(doc_packet);
	connection.clear_bytes();

	CredentialInfos credential;

	const std::string credential_file{ "credential.dat" };
	if (std::filesystem::exists(credential_file)) {
		std::cout << "Credential file detected" << std::endl;

		credential.read_on_disk(credential_file);
		
	} else {
		std::cout << "Credential file not detected" << std::endl;

		send_packet(connection, Packet{ PacketType::Register });
		const Packet register_packet = recv_packet(connection);
		connection.clear_bytes();
		switch (register_packet.type) {
			case PacketType::Registered:
				credential = handle_registered_packet(register_packet);
				break;
			case PacketType::Result: {
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

	credential.pprint();

	send_packet(connection, write_login_packet(credential));
	const Result login_result = handle_result_packet(recv_packet(connection));
	if (login_result.error()) {
		login_result.pprint();
		throw std::runtime_error("Error: Could not login using credential");
	}
	const Status status = handle_status_packet(recv_packet(connection));

	std::cout << "Retriving " << status.nb_mails.value_or(0) << " mails" << std::endl;
	std::vector<Mail> mails;
	for(uint32_t i = 1; i <= status.nb_mails.value_or(0); ++i) {
		send_packet(connection, write_getmail_packet(i));
		const Mail mail = handle_mail_packet(recv_packet(connection));

		const std::string filename = "./mail_" + std::to_string(i) + ".txt";
		mail.save_on_disk(filename);
		mails.push_back(mail);
	}

	// Second mail need translation
	if (mails.size() < 2) {
		throw std::runtime_error("Error: No second email retrived");
	}

	Dictionnary rasvakian_dict;
	const std::string dict_filename{ "rasvakian_dict.txt" };
	if (std::filesystem::exists(dict_filename)) {
		rasvakian_dict.read_on_disk(dict_filename);
	}

	Mail& rasvakian_mail = mails[1];
	std::vector<std::string> rasvakian_words = get_unique_words(rasvakian_mail.content);
	std::cout << rasvakian_words.size() << " words to translate" << std::endl;
	for(size_t i = 0; i < rasvakian_words.size(); ++i) {
		if (rasvakian_dict.contains(rasvakian_words[i]))
			continue;

		send_packet(connection, write_translate_packet(rasvakian_words[i]));

		const Packet translation_result = recv_packet(connection);
		switch (translation_result.type) {
			case PacketType::Result: {
				const Result error = handle_result_packet(translation_result);
				error.pprint();
				throw std::runtime_error("Error: could not translate word");
			}
			case PacketType::Translation: {
				const std::string translation = handle_translation_packet(translation_result);
				std::cout << rasvakian_words[i] << " -> " << translation << std::endl;
				rasvakian_dict[rasvakian_words[i]] = translation;
				break;
			}
			default:
				translation_result.pprint();
				throw std::runtime_error("Error: Unexpected packet type during translation");
		}

		// TODO: Do not write at each iteration
		rasvakian_dict.save_on_disk(dict_filename);
		std::this_thread::sleep_for(std::chrono::seconds(60));
	}

	rasvakian_mail.translate(rasvakian_dict);

	std::cout << rasvakian_mail.content << std::endl;

	// const std::string config_file{ "configuration.dat" };
	// if (!std::filesystem::exists(config_file)) {
	// 	throw std::runtime_error("Error: Configuration file not found");
	// }
	// Configuration config;
	// config.read_on_disk(config_file);
	// config.pprint();

	// send_packet(connection, write_configuration_packet(config));
	// const Result config_result = handle_result_packet(recv_packet(connection));
	// config_result.pprint();
	

	return 0;
}
