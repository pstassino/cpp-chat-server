#include <cstdlib>
#include <deque>
#include <iostream>
#include <thread>
#include <fstream>
#include <boost/asio.hpp>
#include "chat_message.h"

using boost::asio::ip::tcp;

typedef std::deque<Message> chat_message_queue;

std::ofstream myfile("data.txt", std::ofstream::out | std::ofstream::app);


class Chat_client
{
public:
	Chat_client(boost::asio::io_service& io_service,
		tcp::resolver::iterator endpoint_iterator)
		: m_io_service(io_service),
		  m_socket(io_service)
	{
		do_connect(endpoint_iterator);
	}

	void write(const Message& msg)
	{
		m_io_service.post(
			[this, msg]()
		{
			bool write_in_progress = !write_msgs.empty();
			write_msgs.push_back(msg);
			if (!write_in_progress) {
				do_write();
			}
		});
	}

	void close()
	{
		m_io_service.post([this]() { m_socket.close(); });
	}

private:
	void do_connect(tcp::resolver::iterator endpoint_iterator)
	{
		boost::asio::async_connect(m_socket, endpoint_iterator,
			[this](boost::system::error_code ec, tcp::resolver::iterator)
		{
			if (!ec) {
				std::cout << "Connected to Server" << '\n';
				do_read_header();
			}
		});
	}

	void do_read_header()
	{
		boost::asio::async_read(m_socket,
			boost::asio::buffer(read_msg.data(), Message::header_length),
			[this](boost::system::error_code ec, std::size_t /*length*/)
		{
			if (!ec && read_msg.decode_header()) {
				do_read_body();
			}
			else {
				m_socket.close();
				std::cout << "Disconnected from Server" << '\n';
			}
		});
	}

	void do_read_body()
	{
		boost::asio::async_read(m_socket,
			boost::asio::buffer(read_msg.body(), read_msg.body_length()),
			[this](boost::system::error_code ec, std::size_t)
		{
			if (!ec) {
				SYSTEMTIME st;
				GetSystemTime(&st);
				char timestamp[13 + 1] = "";
				std::sprintf(timestamp, " %02d:%02d:%02d.%03d", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
				std::cout.write(read_msg.body(), read_msg.body_length());
				std::cout << timestamp << "\n";
				do_write_file(read_msg, timestamp);
				do_read_header();
			}
			else {
				m_socket.close();
				std::cout << "Disconnected from Server" << '\n';
			}
		});
	}

	void do_write()
	{
		boost::asio::async_write(m_socket,
			boost::asio::buffer(write_msgs.front().data(),
				write_msgs.front().length()),
			[this](boost::system::error_code ec, std::size_t)
		{
			if (!ec) {
				write_msgs.pop_front();
				if (!write_msgs.empty()) {
					do_write();
				}
			}
			else {
				m_socket.close();
				std::cout << "Disconnected from Server" << '\n';
			}
		});
	}

	void do_write_file(Message msg, char* timestamp)
	{
		if (myfile.is_open()) {
			char temp[700] = { 0 };
			std::memcpy(temp, msg.body(), msg.body_length());
			std::memcpy(temp + msg.body_length(), timestamp, 13);
			myfile << temp << '\n';
		}
		else {
			std::cout << "Unable to open file" << '\n';
		}
	}

	boost::asio::io_service& m_io_service;
	tcp::socket m_socket;
	Message read_msg;
	chat_message_queue write_msgs;
};

int main(int argc, char* argv[])
{
	try {
		if (argc != 3) {
			std::cerr << "Usage: chat_client <host> <port>\n";
			return 1;
		}

		boost::asio::io_service io_service;

		tcp::resolver resolver(io_service);
		auto endpoint_iterator = resolver.resolve({ argv[1], argv[2] });
		Chat_client c(io_service, endpoint_iterator);

		std::thread t(
			[&io_service]() { 
			io_service.run(); 
		});

		char line[Message::max_body_length + 1];
		while (std::cin.getline(line, Message::max_body_length + 1)) {
			Message msg;
			msg.body_length(std::strlen(line));
			std::memcpy(msg.body(), line, msg.body_length());
			msg.encode_header();
			c.write(msg);
		}

		c.close();
		myfile.close();
		t.join();
	}
	catch (std::exception& e) {
		std::cerr << "Exception: " << e.what() << "\n";
	}
	return 0;
}