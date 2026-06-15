#include "Channel.hpp"
#include <cstdio>

Channel::Channel(const std::string& name)
    : _name(name), _inviteOnly(false), _topicRestricted(false), _userLimit(0) {}

Channel::~Channel() {}

const std::string& Channel::getName() const {
    return _name;
}

void Channel::addClient(Client* client) {
    if (_clients.empty())
        _operators[client->getFd()] = client;
    _clients[client->getFd()] = client;
    clearInvite(client->getFd());
}

void Channel::removeClient(int clientFd) {
    _clients.erase(clientFd);
    _operators.erase(clientFd);
    _invited.erase(clientFd);
}

bool Channel::hasClient(int clientFd) const {
    return _clients.find(clientFd) != _clients.end();
}

size_t Channel::getClientCount() const {
    return _clients.size();//tamaño del canal en clientes participando
}

std::string Channel::getMemberList() const {
    std::string list;
    for (std::map<int, Client*>::const_iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (!list.empty())
            list += " ";
        if (isOperator(it->first))
            list += "@";
        list += it->second->getNickname();
    }
    return list;
}

void Channel::setTopic(const std::string& topic) {
    _topic = topic;
}

const std::string& Channel::getTopic() const {
    return _topic;
}

bool Channel::isOperator(int clientFd) const {
    return _operators.find(clientFd) != _operators.end(); //devuelve operadores del canal
}

void Channel::addOperator(int clientFd) {
    if (hasClient(clientFd))
        _operators[clientFd] = _clients.find(clientFd)->second;
}

void Channel::removeOperator(int clientFd) {
    _operators.erase(clientFd);
}

bool Channel::isInviteOnly() const { return _inviteOnly; }
bool Channel::isTopicRestricted() const { return _topicRestricted; }
const std::string& Channel::getKey() const { return _key; }
int Channel::getUserLimit() const { return _userLimit; }

std::string Channel::getModeString() const {
    std::string modes = "+";
    std::string params;
    if (_inviteOnly)
        modes += "i";
    if (_topicRestricted)
        modes += "t";
    if (!_key.empty()) {
        modes += "k";
        params += " " + _key;
    }
    if (_userLimit > 0) {
        modes += "l";
        char buf[32];
        std::sprintf(buf, "%d", _userLimit);
        params += " ";
        params += buf;
    }
    if (modes == "+")
        return "";
    return modes + params;
}

void Channel::setInviteOnly(bool value) { _inviteOnly = value; }
void Channel::setTopicRestricted(bool value) { _topicRestricted = value; }

void Channel::setKey(const std::string& key) {
    _key = key;
}

void Channel::clearKey() {
    _key.clear();
}

void Channel::setUserLimit(int limit) {
    _userLimit = limit;
}

bool Channel::isInvited(int clientFd) const {
    return _invited.find(clientFd) != _invited.end();
}

void Channel::inviteClient(int clientFd) {
    _invited.insert(clientFd);
}

void Channel::clearInvite(int clientFd) {
    _invited.erase(clientFd);
}

bool Channel::canJoin(Client* client, const std::string& key) const {
    if (_userLimit > 0 && _clients.size() >= static_cast<size_t>(_userLimit))
        return false;
    if (_inviteOnly && !isInvited(client->getFd()))
        return false;
    if (!_key.empty() && _key != key)
        return false;
    return true;
}

bool Channel::canChangeTopic(int clientFd) const {
    if (!hasClient(clientFd))
        return false;
    if (_topicRestricted)
        return isOperator(clientFd);
    return true;
}
