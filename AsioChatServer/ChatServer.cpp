#include <cstdlib>
#include <deque>
#include <iostream>
#include <list>
#include <memory>
#include <set>
#include <utility>
#include <boost/asio.hpp>
#include "Chat_message.h"

using boost::asio::ip::tcp;

typedef std::deque<Message> chat_message_queue;

//----------------------------------------------------------------------

class chat_participant
{
public:
	virtual ~chat_participant() {}

	virtual void deliver(const Message& msg) = 0;
};

//----------------------------------------------------------------------

typedef std::shared_ptr<chat_participant> chat_participant_ptr;

//----------------------------------------------------------------------

class chat_room
{
public:
	void join(chat_participant_ptr participant)
	{
		m_participants.insert(participant);
		for (auto msg : m_recent_msgs)
			participant->deliver(msg);
	}

	void leave(chat_participant_ptr participant)
	{
		m_participants.erase(participant);
	}

	void deliver(const Message& msg)
	{
		m_recent_msgs.push_back(msg);

		while (m_recent_msgs.size() > max_recent_msgs)
			m_recent_msgs.pop_front();

		for (auto participant : m_participants)
			participant->deliver(msg);
	}

private:
	std::set<chat_participant_ptr> m_participants;
	chat_message_queue m_recent_msgs;
	enum { max_recent_msgs = 100 };
};

//----------------------------------------------------------------------

class chat_session: public chat_participant,
	public std::enable_shared_from_this<chat_session>
{
public:
	chat_session(tcp::socket socket, chat_room& room) 
		: m_socket(std::move(socket)), 
		  m_room(room){}

	void start()
	{
		m_room.join(shared_from_this());
		do_read_header();
	}

	void deliver(const Message& msg)
	{
		bool write_in_progress = !m_write_msgs.empty();
		m_write_msgs.push_back(msg);

		if (!write_in_progress){
			do_write();
		}
	}

private:
	void do_read_header()
	{
		auto self(shared_from_this());

		boost::asio::async_read(m_socket,
			boost::asio::buffer(m_read_msg.data(), Message::header_length),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
		{
			if (!ec && m_read_msg.decode_header()){
				do_read_body();
			}
			else{
				m_room.leave(shared_from_this());
			}
		});
	}

	void do_read_body()
	{
		auto self(shared_from_this());

		boost::asio::async_read(m_socket,
			boost::asio::buffer(m_read_msg.body(), m_read_msg.body_length()),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
		{
			if (!ec){
				m_room.deliver(m_read_msg);
				do_read_header();
			}
			else{
				m_room.leave(shared_from_this());
			}
		});
	}

	void do_write()
	{
		auto self(shared_from_this());

		boost::asio::async_write(m_socket,
			boost::asio::buffer(m_write_msgs.front().data(), m_write_msgs.front().length()),
			[this, self](boost::system::error_code ec, std::size_t /*length*/)
		{
			if (!ec)
			{
				m_write_msgs.pop_front();
				if (!m_write_msgs.empty())
				{
					do_write();
				}
			}
			else
			{
				m_room.leave(shared_from_this());
			}
		});
	}

	tcp::socket m_socket;
	chat_room& m_room;
	Message m_read_msg;
	chat_message_queue m_write_msgs;
};

//----------------------------------------------------------------------

class chat_server
{
public:
	chat_server(boost::asio::io_service& io_service, const tcp::endpoint& endpoint)
		: m_acceptor(io_service, endpoint), 
		  m_socket(io_service)
	{
		do_accept();
	}

private:
	void do_accept()
	{
		m_acceptor.async_accept(m_socket, [this](boost::system::error_code ec)
		{
			if (!ec){
				std::make_shared<chat_session>(std::move(m_socket), m_room)->start();
			}
			do_accept();
		});
	}

	tcp::acceptor m_acceptor;
	tcp::socket m_socket;
	chat_room m_room;
};

//----------------------------------------------------------------------

int main(int argc, char* argv[])
{
	try{
		if (argc < 2){
			std::cerr << "Usage: chat_server <port> [<port> ...]\n";
			return 1;
		}

		boost::asio::io_service io_service;

		std::list<chat_server> servers;
		for (int i = 1; i < argc; ++i){
			tcp::endpoint endpoint(tcp::v4(), std::atoi(argv[i]));
			servers.emplace_back(io_service, endpoint);
		}

		io_service.run();
	}
	catch (std::exception& e)
	{
		std::cerr << "Exception: " << e.what() << "\n";
	}
	return 0;
}