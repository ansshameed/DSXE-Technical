#include <iostream>
#include <boost/asio.hpp>
#include <boost/system/error_code.hpp>  

#include "tcpconnection.hpp"

namespace asio = boost::asio;

asio::awaitable<void> TCPConnection::send(std::string_view message, bool async)
{
    try {
        // Push message to queue
        queue_.push(std::string{message});

        // Notify of new message in queue
        timer_.cancel_one();
    }
    catch (std::exception& e)
    {
        std::cout << "Failed to send message. Exception " << e.what() << "\n";
    }

    co_return;
}

asio::awaitable<std::string> TCPConnection::read(std::string& read_buffer)
{
    // Read until the delimiter
    std::size_t n = co_await asio::async_read_until(socket_, asio::dynamic_buffer(read_buffer, 4096), "#END#", asio::use_awaitable);

    // Extract the message from the buffer
    std::string msg = read_buffer.substr(0, n);

    // Consume the message length worth of bytes from the read buffer
    read_buffer.erase(0, n);
    
    co_return msg;
}

bool TCPConnection::open()
{
    return socket_.is_open();
}

void TCPConnection::close()
{
    // Close the socket and connection if not closed already
    if (socket_.is_open())
    {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}