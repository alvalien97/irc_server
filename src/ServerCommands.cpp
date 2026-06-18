#include "Server.hpp"
#include <cctype>

void Server::_tryRegisterClient(Client* client) {
    if (!client->isReadyForWelcome())
        return;

    const std::string& nick = client->getNickname();
    client->setAuthenticated();

    _sendNumeric(client, "001 " + nick + " :Bienvenido, " + nick);
    _sendNumeric(client, "002 " + nick + " :Your host is " + SERVER_NAME);
    _sendNumeric(client, "003 " + nick + " :Server created by alvalien and little isra");
    _sendNumeric(client, "004 " + nick + " " + SERVER_NAME + " 1.0 o o");
}

void Server::_handlePass(Client* client, const std::string& params) {
    if (client->isAuthenticated()) {
        _sendNumeric(client, "462 :You may not reregister");
        return;
    }
    if (params == _password)
        client->setPassed();
    else
        _sendNumeric(client, "464 :Password incorrect");
}

void Server::_handleNick(Client* client, const std::string& params) {
    if (!client->hasPassed()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }
    if (params.empty()) {
        _sendNumeric(client, "431 :No nickname given");
        return;
    }

    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (it->second->getNickname() == params && it->second->getFd() != client->getFd()) {
            _sendNumeric(client, "433 * " + params + " :Nickname is already in use");
            return;
        }
    }

    if (client->isAuthenticated()) {
        std::string oldNick = client->getNickname();
        std::string nickMsg = _clientPrefix(client) + " NICK " + params;
        
        std::set<int> recipients;
        recipients.insert(client->getFd());
        
        for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
            if (it->second->hasClient(client->getFd())) {
                for (std::map<int, Client*>::iterator cit = _clients.begin(); cit != _clients.end(); ++cit) {
                    if (it->second->hasClient(cit->first)) {
                        recipients.insert(cit->first);
                    }
                }
            }
        }
        
        client->setNickname(params);
        for (std::set<int>::iterator rit = recipients.begin(); rit != recipients.end(); ++rit) {
            _queueForSend(*rit, nickMsg);
        }
        return;
    }

    client->setNickname(params);
    _tryRegisterClient(client);
}

void Server::_handleUser(Client* client, const std::string& params) {
    if (!client->hasPassed()) {
        _sendNumeric(client, "451 :You have not registered");
        return;
    }
    if (client->hasUser()) {
        _sendNumeric(client, "462 :You may not reregister");
        return;
    }

    size_t space1 = params.find(' ');
    if (space1 == std::string::npos) {
        _sendNumeric(client, "461 USER :Not enough parameters");
        return;
    }

    std::string username = params.substr(0, space1);
    size_t colon = params.find(':');
    std::string realname;
    if (colon != std::string::npos) {
        realname = params.substr(colon + 1);
    } else {
        realname = "Unknown";
    }

    client->setUsername(username);
    client->setRealname(realname);
    client->setUserDone();
    _tryRegisterClient(client);
}

void Server::_handleQuit(Client* client, const std::string& params) {
    std::string reason;
    if (params.empty()) {
        reason = "Quit";
    } else {
        reason = params;
    }

    if (!reason.empty() && reason[0] == ':')
        reason = reason.substr(1);

    std::string quitMsg = _clientPrefix(client) + " QUIT :" + reason;
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        if (it->second->hasClient(client->getFd()))
            _broadcastToChannel(it->second, quitMsg, client->getFd());
    }
    _disconnectClient(client->getFd());
}

void Server::_handlePing(Client* client, const std::string& params) {
    if (params.empty())
        _queueForSend(client->getFd(), "PONG :" + SERVER_NAME);
    else
        _queueForSend(client->getFd(), "PONG " + SERVER_NAME + " :" + params);
}

void Server::_handleCap(Client* client, const std::string& params) {
    (void)params;
    _queueForSend(client->getFd(), "CAP * LS :");
    _queueForSend(client->getFd(), "CAP * END");
}

void Server::_processMessage(Client* client, const std::string& message) {
    if (message.empty())
        return;

    size_t spacePos = message.find(' ');
    std::string command;
    std::string params;
    if (spacePos != std::string::npos) {
        command = message.substr(0, spacePos);
        params = message.substr(spacePos + 1);
    } else {
        command = message;
        params = "";
    }

    for (size_t i = 0; i < command.length(); ++i)
        command[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(command[i])));

    if (command == "PASS")
        _handlePass(client, params);
    else if (command == "NICK")
        _handleNick(client, params);
    else if (command == "USER")
        _handleUser(client, params);
    else if (command == "JOIN")
        _handleJoin(client, params);
    else if (command == "PART")
        _handlePart(client, params);
    else if (command == "PRIVMSG")
        _handlePrivmsg(client, params);
    else if (command == "QUIT")
        _handleQuit(client, params);
    else if (command == "TOPIC")
        _handleTopic(client, params);
    else if (command == "KICK")
        _handleKick(client, params);
    else if (command == "INVITE")
        _handleInvite(client, params);
    else if (command == "MODE")
        _handleMode(client, params);
    else if (command == "PING")
        _handlePing(client, params);
    else if (command == "CAP")
        _handleCap(client, params);
    else if (command == "NOTICE")
        ; // ignore
    else
        _sendNumeric(client, "421 " + command + " :Unknown command");
}