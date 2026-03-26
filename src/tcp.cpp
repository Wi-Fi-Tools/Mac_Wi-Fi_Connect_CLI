#include "communication/tcp.h"

/*
TcpServer
*/

TcpServer::TcpServer(int port) : port_(port), server_socket_(-1), connect_socket_(-1) {}
TcpServer::~TcpServer() {
    if (server_socket_ != -1) {
        close(server_socket_);
    }
    if (connect_socket_ != -1) {
        close(connect_socket_);
    }
}
bool TcpServer::Listen() {
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ == -1) {
        return false;
    }
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        return false;
    }
    if (listen(server_socket_, 1) == -1) {
        return false;
    }
    return true;
}
bool TcpServer::Accept() {
    sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    connect_socket_ = accept(server_socket_, (struct sockaddr*)&client_addr, &client_addr_len);
    if (connect_socket_ == -1) {
        return false;
    }
    return true;
}
bool TcpServer::Send(const std::string& message) {
    if (send(connect_socket_, message.c_str(), message.size(), 0) == -1) {
        return false;
    }
    return true;
}
bool TcpServer::Receive(std::string& message) {
    char buffer[1024];
    int bytes_received = recv(connect_socket_, buffer, sizeof(buffer), 0);
    if (bytes_received == -1) {
        return false;
    }
    message.assign(buffer, bytes_received);
    return true;
}
bool TcpServer::Close() {
    if (connect_socket_ != -1) {
        close(connect_socket_);
        connect_socket_ = -1;
    }
    if (server_socket_ != -1) {
        close(server_socket_);
        server_socket_ = -1;
    }
    return true;
}


/*
TcpClient
*/

TcpClient::TcpClient(const std::string& ip, int port) : server_ip_(ip), server_port_(port) {
    client_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket_ == -1) {
        return;
    }
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(server_ip_.c_str());
    server_addr.sin_port = htons(server_port_);
    if (connect(client_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        return;
    }
}
TcpClient::~TcpClient() {
    if (client_socket_ != -1) {
        close(client_socket_);
    }
}
bool TcpClient::Send(const std::string& message) {
    if (send(client_socket_, message.c_str(), message.size(), 0) == -1) {
        return false;
    }
    return true;
}
bool TcpClient::Receive(std::string& message) {
    char buffer[1024];
    int bytes_received = recv(client_socket_, buffer, sizeof(buffer), 0);
    if (bytes_received == -1) {
        return false;
    }
    message.assign(buffer, bytes_received);
    return true;
}
bool TcpClient::Close() {
    if (client_socket_ != -1) {
        close(client_socket_);
        client_socket_ = -1;
    }
    return true;
}
