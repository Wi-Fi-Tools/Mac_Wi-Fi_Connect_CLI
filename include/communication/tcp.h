#ifndef TCP_H
#define TCP_H

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class TcpServer{
public:
    TcpServer(int port);
    ~TcpServer();
    bool Listen();
    bool Accept();
    bool Send(const std::string& message);
    bool Receive(std::string& message);
    bool Close();

private:
    int port_;
    int server_socket_;
    int connect_socket_;
    
};

class TcpClient{
public:
    TcpClient(const std::string& server_ip, int server_port);
    ~TcpClient();
    bool Connect();
    bool Send(const std::string& message);
    bool Receive(std::string& message);
    bool Close();

private:
    int client_socket_;
    std::string server_ip_;
    int server_port_;
    bool connected_;
};

#endif