#include "Server.hpp"
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

const std::string Server::SERVER_NAME = "ft_irc.local";

Server::Server(int port, const std::string& password)
    : _port(port), _password(password), _serverSocket(-1) {
    std::cout << "Server listening on port " << _port << std::endl;
}

Server::~Server() {
    if (_serverSocket != -1)
        close(_serverSocket);

    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        close(it->first);
        delete it->second;
    }
    _clients.clear();

    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it)
        delete it->second;
    _channels.clear();
}

void Server::_initSocket() {
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0)
        throw std::runtime_error("Failed to create socket");

    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("Failed to setsockopt");

    if (fcntl(_serverSocket, F_SETFL, O_NONBLOCK) < 0)
        throw std::runtime_error("Failed to set non-blocking on server socket");

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(_port);

    if (bind(_serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
        throw std::runtime_error("Failed to bind socket to port");

    if (listen(_serverSocket, SOMAXCONN) < 0)
        throw std::runtime_error("Failed to listen on socket");

    struct pollfd pfd;
    pfd.fd = _serverSocket;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pollFds.push_back(pfd);

    std::cout << "Socket bound and listening." << std::endl;
}

void Server::_acceptNewClient() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(_serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);

    if (clientFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        std::cerr << "accept() failed\n";
        return;
    }

    if (fcntl(clientFd, F_SETFL, O_NONBLOCK) < 0) {
        close(clientFd);
        return;
    }

    struct pollfd pfd;
    pfd.fd = clientFd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pollFds.push_back(pfd);

    _clients[clientFd] = new Client(clientFd);
    _outBuffers[clientFd] = "";

    std::cout << "Client connected fd=" << clientFd
              << " ip=" << inet_ntoa(clientAddr.sin_addr) << std::endl;
}

void Server::_handleClientData(int clientFd) {
    char buf[512];
    std::memset(buf, 0, sizeof(buf));

    int bytesRead = recv(clientFd, buf, sizeof(buf) - 1, 0);
    if (bytesRead <= 0) {
        _disconnectClient(clientFd);
        return;
    }

    std::map<int, Client*>::iterator it = _clients.find(clientFd);
    if (it == _clients.end())
        return;

    Client* client = it->second;
    client->appendBuffer(std::string(buf, bytesRead));

    // Procesamos en bucle todos los comandos completos que hayan entrado en el buffer
    while (true) {
        std::map<int, Client*>::iterator currentIt = _clients.find(clientFd);
        
        // Control de seguridad: Si el cliente fue desconectado en el comando anterior (ej. QUIT)
        // o simplemente ya no tiene más mensajes completos (\n), rompemos el bucle de forma segura.
        if (currentIt == _clients.end() || !currentIt->second->hasCompleteMessage()) {
            break;
        }

        Client* currClient = currentIt->second;
        std::string msg = currClient->extractMessage();
        _processMessage(currClient, msg);
    }
}

void Server::_disconnectClient(int clientFd) {
    Client* client = 0;
    if (_clients.find(clientFd) != _clients.end())
        client = _clients[clientFd];

    if (client && client->hasNickname()) {
        std::string quitMsg = _clientPrefix(client) + " QUIT :Client disconnected";
        for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
            if (it->second->hasClient(clientFd))
                _broadcastToChannel(it->second, quitMsg, clientFd);
        }
    }

    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        if (it->second->hasClient(clientFd))
            it->second->removeClient(clientFd);
    }
    _removeEmptyChannels();

    close(clientFd);
    _outBuffers.erase(clientFd);
    _pendingClose.erase(clientFd);

    if (_clients.find(clientFd) != _clients.end()) {
        delete _clients[clientFd];
        _clients.erase(clientFd);
    }

    for (std::vector<struct pollfd>::iterator it = _pollFds.begin(); it != _pollFds.end(); ++it) {
        if (it->fd == clientFd) {
            _pollFds.erase(it);
            break;
        }
    }

    std::cout << "Client disconnected fd=" << clientFd << std::endl;
}

void Server::start() {
    _initSocket();
    std::cout << "Waiting for connections..." << std::endl;
    while (g_serverRunning) {
        int pollCount = poll(&_pollFds[0], _pollFds.size(), -1);
        if (pollCount < 0) {
            if (errno == EINTR)
                continue;
            std::cerr << "poll() failed\n";
            break;
        }

        size_t i = 0;
        while (i < _pollFds.size()) {
            int fd = _pollFds[i].fd;
            short revents = _pollFds[i].revents;
            size_t oldSize = _pollFds.size();

            if (revents & (POLLERR | POLLNVAL)) {
                if (fd != _serverSocket)
                    _scheduleDisconnect(fd);
            } else {
                if (revents & POLLIN) {
                    if (fd == _serverSocket)
                        _acceptNewClient();
                    else
                        _handleClientData(fd);
                }

                if (revents & POLLOUT)
                    _flushOutgoing(fd);

                if ((revents & POLLHUP) && fd != _serverSocket)
                    _scheduleDisconnect(fd);
            }

            if (_pollFds.size() < oldSize) {
                continue;
            }
            ++i;
        }
        _flushWritableClients();
    }
}
