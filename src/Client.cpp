#include "Client.hpp"

Client::Client(int fd)
    : _fd(fd), _isAuthenticated(false), _hasUser(false), _hasPassed(false) {}

Client::~Client() {}

int Client::getFd() const {
    return _fd;
}

void Client::appendBuffer(const std::string& data) {
    _buffer += data;
}

bool Client::hasCompleteMessage() const {
    return _buffer.find('\n') != std::string::npos;
}

std::string Client::extractMessage() {
    size_t pos = _buffer.find('\n');
    if (pos == std::string::npos)
        return "";

    std::string msg = _buffer.substr(0, pos);
    _buffer.erase(0, pos + 1);

    if (!msg.empty() && msg[msg.length() - 1] == '\r')
        msg.erase(msg.length() - 1);

    return msg;
}

const std::string& Client::getNickname() const { return _nickname; }
const std::string& Client::getUsername() const { return _username; }
const std::string& Client::getRealname() const { return _realname; }

bool Client::hasNickname() const {
    return !_nickname.empty();
}

bool Client::hasUser() const {
    return _hasUser;
}

bool Client::isAuthenticated() const {
    return _isAuthenticated;
}

bool Client::hasPassed() const {
    return _hasPassed;
}

bool Client::isReadyForWelcome() const {
    return _hasPassed && hasNickname() && _hasUser && !_isAuthenticated;
}

void Client::setNickname(const std::string& nickname) {
    _nickname = nickname;
}

void Client::setUsername(const std::string& username) {
    _username = username;
}

void Client::setRealname(const std::string& realname) {
    _realname = realname;
}

void Client::setPassed() {
    _hasPassed = true;
}

void Client::setUserDone() {
    _hasUser = true;
}

void Client::setAuthenticated() {
    _isAuthenticated = true;
}
