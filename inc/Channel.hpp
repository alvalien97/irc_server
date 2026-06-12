#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <string>
#include <map>
#include <set>
#include "Client.hpp"

class Channel {
private:
    std::string _name;
    std::string _topic;
    std::map<int, Client*> _clients;
    std::map<int, Client*> _operators;
    std::set<int> _invited;
    bool _inviteOnly;
    bool _topicRestricted;
    std::string _key;
    int _userLimit;

public:
    Channel(const std::string& name);
    ~Channel();

    const std::string& getName() const;
    void addClient(Client* client);
    void removeClient(int clientFd);
    bool hasClient(int clientFd) const;
    size_t getClientCount() const;
    std::string getMemberList() const;

    void setTopic(const std::string& topic);
    const std::string& getTopic() const;
    bool isOperator(int clientFd) const;
    void addOperator(int clientFd);
    void removeOperator(int clientFd);

    bool isInviteOnly() const;
    bool isTopicRestricted() const;
    const std::string& getKey() const;
    int getUserLimit() const;
    std::string getModeString() const;

    void setInviteOnly(bool value);
    void setTopicRestricted(bool value);
    void setKey(const std::string& key);
    void clearKey();
    void setUserLimit(int limit);

    bool isInvited(int clientFd) const;
    void inviteClient(int clientFd);
    void clearInvite(int clientFd);

    bool canJoin(Client* client, const std::string& key) const;
    bool canChangeTopic(int clientFd) const;
};

#endif
