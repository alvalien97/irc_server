#include "Server.hpp"
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

void Server::_setPollout(int clientFd, bool enable) {
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        if (_pollFds[i].fd == clientFd) {
            if (enable)
                _pollFds[i].events |= POLLOUT;
            else
                _pollFds[i].events &= ~POLLOUT;
            return;
        }
    }
}

void Server::_queueForSend(int clientFd, const std::string& message) {
    std::string line = message;
    if (line.size() < 2 || line.substr(line.size() - 2) != "\r\n")
        line += "\r\n";
    _outBuffers[clientFd] += line;
    _setPollout(clientFd, true);
}

void Server::_flushOutgoing(int clientFd) {
    std::map<int, std::string>::iterator bufIt = _outBuffers.find(clientFd);
    if (bufIt == _outBuffers.end() || bufIt->second.empty()) {
        _setPollout(clientFd, false);
        return;
    }

    ssize_t sent = send(clientFd, bufIt->second.c_str(), bufIt->second.length(), 0);
    if (sent < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        _disconnectClient(clientFd);
        return;
    }

    bufIt->second.erase(0, static_cast<size_t>(sent));
    if (bufIt->second.empty()) {
        _setPollout(clientFd, false);
        if (_pendingClose.find(clientFd) != _pendingClose.end())
            _disconnectClient(clientFd);
    }
}

bool Server::_hasPendingWrites() const {
    for (std::map<int, std::string>::const_iterator it = _outBuffers.begin(); it != _outBuffers.end(); ++it) {
        if (!it->second.empty())
            return true;
    }
    return false;
}

void Server::_flushWritableClients() {
    if (!_hasPendingWrites())
        return;

    poll(&_pollFds[0], _pollFds.size(), 0);

    std::vector<int> fdsToFlush;
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        if (_pollFds[i].revents & POLLOUT) {
            fdsToFlush.push_back(_pollFds[i].fd);
        }
    }

    for (size_t i = 0; i < fdsToFlush.size(); ++i) {
        int fd = fdsToFlush[i];
        if (_clients.find(fd) != _clients.end()) {
            _flushOutgoing(fd);
        }
    }
}

void Server::_scheduleDisconnect(int clientFd) {
    if (_outBuffers.find(clientFd) == _outBuffers.end() || _outBuffers[clientFd].empty())
        _disconnectClient(clientFd);
    else
        _pendingClose.insert(clientFd);
}

void Server::_broadcastToChannel(Channel* channel, const std::string& message, int exceptFd) {
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (channel->hasClient(it->first) && it->first != exceptFd)
            _queueForSend(it->first, message);
    }
}

std::string Server::_clientPrefix(Client* client) const {
    return ":" + client->getNickname() + "!" + client->getUsername() + "@127.0.0.1";
}

void Server::_sendNumeric(Client* client, const std::string& line) {
    _queueForSend(client->getFd(), ":" + SERVER_NAME + " " + line);
}

Client* Server::_findClientByNick(const std::string& nick) {
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second->getNickname() == nick)
            return it->second;
    }
    return 0;
}

int Server::_findFdByNick(const std::string& nick) {
    Client* c = _findClientByNick(nick);
    if (c)
        return c->getFd();
    return -1;
}

Channel* Server::_getChannel(const std::string& name) {
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it == _channels.end())
        return 0;
    return it->second;
}

void Server::_removeEmptyChannels() {
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ) {
        if (it->second->getClientCount() == 0) {
            delete it->second;
            _channels.erase(it++);
        } else {
            ++it;
        }
    }
}