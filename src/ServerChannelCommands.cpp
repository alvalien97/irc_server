#include "Server.hpp"
#include "Bot.hpp"
#include <cstdlib>
#include <cstdio>

void Server::_joinChannel(Client* client, Channel* channel, const std::string& channelName) {
    if (channel->hasClient(client->getFd()))
        return;

    channel->addClient(client);

    std::string joinMsg = _clientPrefix(client) + " JOIN :" + channelName;
    _queueForSend(client->getFd(), joinMsg);
    _broadcastToChannel(channel, joinMsg, client->getFd());

    if (channel->getTopic().empty())
        _sendNumeric(client, "331 " + client->getNickname() + " " + channelName + " :No topic is set");
    else
        _sendNumeric(client, "332 " + client->getNickname() + " " + channelName + " :" + channel->getTopic());

    std::string names = channel->getMemberList();
    _sendNumeric(client, "353 " + client->getNickname() + " = " + channelName + " :" + names);
    _sendNumeric(client, "366 " + client->getNickname() + " " + channelName + " :End of /NAMES list");
}

void Server::_handleJoin(Client* client, const std::string& params) {
    if (!client->isAuthenticated()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }
    if (params.empty()) {
        _sendNumeric(client, "461 JOIN :Not enough parameters");
        return;
    }

    size_t sp = params.find(' ');
    std::string channelName;
    std::string key;

    if (sp == std::string::npos) {
        channelName = params;
        key = "";
    } else {
        channelName = params.substr(0, sp);
        key = params.substr(sp + 1);
    }
    if (!key.empty() && key[0] == ':')
        key = key.substr(1);

    if (channelName.empty() || channelName[0] != '#') {
        _sendNumeric(client, "403 " + channelName + " :No such channel");
        return;
    }

    if (_channels.find(channelName) == _channels.end())
        _channels[channelName] = new Channel(channelName);

    Channel* channel = _channels[channelName];

    if (!channel->canJoin(client, key)) {
        if (channel->isInviteOnly())
            _sendNumeric(client, "473 " + channelName + " :Cannot join channel (+i)");
        else if (channel->getUserLimit() > 0 && channel->getClientCount() >= static_cast<size_t>(channel->getUserLimit()))
            _sendNumeric(client, "471 " + channelName + " :Cannot join channel (+l)");
        else
            _sendNumeric(client, "475 " + channelName + " :Cannot join channel (+k)");
        return;
    }

    _joinChannel(client, channel, channelName);
}

void Server::_handlePart(Client* client, const std::string& params) {
    if (params.empty()) {
        _sendNumeric(client, "461 PART :Not enough parameters");
        return;
    }

    std::string channelName;
    std::string reason = "";
    
    size_t spacePos = params.find(' ');
    if (spacePos == std::string::npos) {
        channelName = params;
    } else {
        channelName = params.substr(0, spacePos);
        std::string rest = params.substr(spacePos + 1);
        if (!rest.empty() && rest[0] == ':') {
            reason = rest.substr(1);
        } else {
            reason = rest;
        }
    }

    Channel* channel = _getChannel(channelName);
    if (!channel) {
        _sendNumeric(client, "403 " + client->getNickname() + " " + channelName + " :No such channel");
        return;
    }

    if (!channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + client->getNickname() + " " + channelName + " :You're not on that channel");
        return;
    }

    std::string partMsg = _clientPrefix(client) + " PART " + channelName;
    if (!reason.empty()) {
        partMsg += " :" + reason;
    }
    partMsg += "\r\n";

    _broadcastToChannel(channel, partMsg, -1);
    channel->removeClient(client->getFd());
    _removeEmptyChannels();
}

void Server::_handleBotPrivmsg(Client* client, const std::string& target, const std::string& text) {
    std::string reply = Bot::handleMessage(client, text);
    std::string msg = Bot::buildReply(target, reply);
    if (target[0] == '#') {
        Channel* channel = _getChannel(target);
        if (!channel || !channel->hasClient(client->getFd())) {
            _sendNumeric(client, "404 " + target + " :Cannot send to channel");
            return;
        }
        _broadcastToChannel(channel, msg, -1);
    } else {
        _queueForSend(client->getFd(), msg);
    }
}

void Server::_handlePrivmsg(Client* client, const std::string& params) {
    if (!client->isAuthenticated()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }

    size_t spacePos = params.find(' ');
    if (spacePos == std::string::npos) {
        _sendNumeric(client, "411 :No recipient given (PRIVMSG)");
        return;
    }

    std::string target = params.substr(0, spacePos);
    std::string text = params.substr(spacePos + 1);
    if (!text.empty() && text[0] == ':')
        text = text.substr(1);

    if (text.empty()) {
        _sendNumeric(client, "412 :No text to send");
        return;
    }

    if (Bot::isBotNick(target) || (target[0] == '#' && Bot::isBotCommand(text))) {
        if (target[0] == '#' && Bot::isBotCommand(text))
            _handleBotPrivmsg(client, target, text);
        else if (Bot::isBotNick(target))
            _handleBotPrivmsg(client, client->getNickname(), text);
        return;
    }

    std::string fullMsg = _clientPrefix(client) + " PRIVMSG " + target + " :" + text;

    if (target[0] == '#') {
        Channel* channel = _getChannel(target);
        if (!channel) {
            _sendNumeric(client, "401 " + target + " :No such nick/channel");
            return;
        }
        if (!channel->hasClient(client->getFd())) {
            _sendNumeric(client, "404 " + target + " :Cannot send to channel");
            return;
        }
        _broadcastToChannel(channel, fullMsg, client->getFd());
    } else {
        int targetFd = _findFdByNick(target);
        if (targetFd == -1) {
            _sendNumeric(client, "401 " + target + " :No such nick/channel");
            return;
        }
        _queueForSend(targetFd, fullMsg);
    }
}

void Server::_handleTopic(Client* client, const std::string& params) {
    if (!client->isAuthenticated()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }

    size_t spacePos = params.find(' ');
    if (spacePos == std::string::npos) {
        std::string channelName = params;
        Channel* channel = _getChannel(channelName);
        if (!channel || !channel->hasClient(client->getFd())) {
            _sendNumeric(client, "442 " + channelName + " :You're not on that channel");
            return;
        }
        if (channel->getTopic().empty())
            _sendNumeric(client, "331 " + client->getNickname() + " " + channelName + " :No topic is set");
        else
            _sendNumeric(client, "332 " + client->getNickname() + " " + channelName + " :" + channel->getTopic());
        return;
    }

    std::string channelName = params.substr(0, spacePos);
    std::string newTopic = params.substr(spacePos + 1);
    if (!newTopic.empty() && newTopic[0] == ':')
        newTopic = newTopic.substr(1);

    Channel* channel = _getChannel(channelName);
    if (!channel) {
        _sendNumeric(client, "403 " + channelName + " :No such channel");
        return;
    }
    if (!channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + channelName + " :You're not on that channel");
        return;
    }
    if (!channel->canChangeTopic(client->getFd())) {
        _sendNumeric(client, "482 " + channelName + " :You're not channel operator");
        return;
    }

    channel->setTopic(newTopic);
    std::string topicMsg = _clientPrefix(client) + " TOPIC " + channelName + " :" + newTopic;
    _queueForSend(client->getFd(), topicMsg);
    _broadcastToChannel(channel, topicMsg, client->getFd());
}

void Server::_handleKick(Client* client, const std::string& params) {
    if (!client->isAuthenticated())
        return;

    size_t space1Pos = params.find(' ');
    if (space1Pos == std::string::npos) {
        _sendNumeric(client, "461 KICK :Not enough parameters");
        return;
    }

    std::string channelName = params.substr(0, space1Pos);
    std::string rest = params.substr(space1Pos + 1);
    size_t space2Pos = rest.find(' ');

    std::string targetUser;
    std::string reason;

    if (space2Pos != std::string::npos) {
        targetUser = rest.substr(0, space2Pos);
        reason = rest.substr(space2Pos + 1);
    } else {
        targetUser = rest;
        reason = "Kicked";
    }

    if (!reason.empty() && reason[0] == ':')
        reason = reason.substr(1);

    Channel* channel = _getChannel(channelName);
    if (!channel) {
        _sendNumeric(client, "403 " + channelName + " :No such channel");
        return;
    }
    if (!channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + channelName + " :You're not on that channel");
        return;
    }
    if (!channel->isOperator(client->getFd())) {
        _sendNumeric(client, "482 " + channelName + " :You're not channel operator");
        return;
    }

    int targetFd = _findFdByNick(targetUser);
    if (targetFd == -1 || !channel->hasClient(targetFd)) {
        _sendNumeric(client, "441 " + targetUser + " " + channelName + " :They aren't on that channel");
        return;
    }

    std::string kickMsg = _clientPrefix(client) + " KICK " + channelName + " " + targetUser + " :" + reason;
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (channel->hasClient(it->first))
            _queueForSend(it->first, kickMsg);
    }

    channel->removeClient(targetFd);
    _removeEmptyChannels();
}

void Server::_handleInvite(Client* client, const std::string& params) {
    if (!client->isAuthenticated())
        return;

    size_t spacePos = params.find(' ');
    if (spacePos == std::string::npos) {
        _sendNumeric(client, "461 INVITE :Not enough parameters");
        return;
    }

    std::string targetUser = params.substr(0, spacePos);
    std::string channelName = params.substr(spacePos + 1);
    if (!channelName.empty() && channelName[0] == ':')
        channelName = channelName.substr(1);

    Channel* channel = _getChannel(channelName);
    if (!channel || !channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + channelName + " :You're not on that channel");
        return;
    }
    if (!channel->isOperator(client->getFd())) {
        _sendNumeric(client, "482 " + channelName + " :You're not channel operator");
        return;
    }

    int targetFd = _findFdByNick(targetUser);
    if (targetFd == -1) {
        _sendNumeric(client, "401 " + targetUser + " :No such nick/channel");
        return;
    }
    if (channel->hasClient(targetFd)) {
        _sendNumeric(client, "443 " + targetUser + " " + channelName + " :is already on channel");
        return;
    }

    channel->inviteClient(targetFd);
    std::string inviteMsg = _clientPrefix(client) + " INVITE " + targetUser + " :" + channelName;
    _queueForSend(targetFd, inviteMsg);
    _sendNumeric(client, "341 " + client->getNickname() + " " + targetUser + " " + channelName);
}

void Server::_handleMode(Client* client, const std::string& params) {
    if (!client->isAuthenticated()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }

    size_t sp = params.find(' ');
    if (sp == std::string::npos) {
        _sendNumeric(client, "461 MODE :Not enough parameters");
        return;
    }

    std::string target = params.substr(0, sp);
    std::string modes = params.substr(sp + 1);
    while (!modes.empty() && modes[0] == ' ')
        modes.erase(0, 1);

    if (target[0] != '#') {
        _sendNumeric(client, "502 :Cant change mode for other users");
        return;
    }

    Channel* channel = _getChannel(target);
    if (!channel) {
        _sendNumeric(client, "403 " + target + " :No such channel");
        return;
    }

    if (modes.empty()) {
        std::string reply = "324 " + client->getNickname() + " " + target + " " + channel->getModeString();
        _sendNumeric(client, reply);
        return;
    }

    if (!channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + target + " :You're not on that channel");
        return;
    }
    if (!channel->isOperator(client->getFd())) {
        _sendNumeric(client, "482 " + target + " :You're not channel operator");
        return;
    }

    bool adding = true;
    size_t i = 0;
    std::string modeReply = _clientPrefix(client) + " MODE " + target + " ";

    while (i < modes.length()) {
        if (modes[i] == '+' || modes[i] == '-') {
            adding = (modes[i] == '+');
            modeReply += modes[i];
            ++i;
            continue;
        }

        char m = modes[i++];

        if (m == 'i') {
            channel->setInviteOnly(adding);
            modeReply += 'i';
        } else if (m == 't') {
            channel->setTopicRestricted(adding);
            modeReply += 't';
        } else if (m == 'k') {
            if (adding) {
                while (i < modes.length() && modes[i] == ' ')
                    ++i;
                size_t end = modes.find_first_of(" +-", i);
                std::string key;
                if (end == std::string::npos) {
                    key = modes.substr(i);
                    i = modes.length();
                } else {
                    key = modes.substr(i, end - i);
                    i = end;
                }
                channel->setKey(key);
                modeReply += " k";
                if (!key.empty())
                    modeReply += " " + key;
            } else {
                channel->clearKey();
                modeReply += " k";
            }
        } else if (m == 'o') {
            while (i < modes.length() && modes[i] == ' ')
                ++i;
            size_t end = modes.find_first_of(" +-", i);
            std::string nick;
            if (end == std::string::npos) {
                nick = modes.substr(i);
                i = modes.length();
            } else {
                nick = modes.substr(i, end - i);
                i = end;
            }
            int fd = _findFdByNick(nick);
            if (fd != -1 && channel->hasClient(fd)) {
                if (adding)
                    channel->addOperator(fd);
                else
                    channel->removeOperator(fd);
                if (adding) {
                    modeReply += " +o ";
                } else {
                    modeReply += " -o ";
                }
                modeReply += nick;
            }
        } else if (m == 'l') {
            if (adding) {
                while (i < modes.length() && modes[i] == ' ')
                    ++i;
                size_t end = modes.find_first_of(" +-", i);
                std::string limStr;
                if (end == std::string::npos) {
                    limStr = modes.substr(i);
                    i = modes.length();
                } else {
                    limStr = modes.substr(i, end - i);
                    i = end;
                }
                int lim = std::atoi(limStr.c_str());
                channel->setUserLimit(lim);
                modeReply += " +l " + limStr;
            } else {
                channel->setUserLimit(0);
                modeReply += " -l";
            }
        }
    }

    _queueForSend(client->getFd(), modeReply);
    _broadcastToChannel(channel, modeReply, client->getFd());
}