#ifndef SERVER_HPP
#define SERVER_HPP

#include <string>
#include <vector>
#include <map>
#include <set>
#include <poll.h>
#include "Client.hpp"
#include "Channel.hpp"

extern bool g_serverRunning;

class Server {
private:
    static const std::string SERVER_NAME;

    int _port;
    std::string _password;
    int _serverSocket;
    std::vector<struct pollfd> _pollFds;
    std::map<int, Client*> _clients;
    std::map<std::string, Channel*> _channels;
    std::map<int, std::string> _outBuffers;
    std::set<int> _pendingClose;

    void _initSocket();
    void _acceptNewClient();
    void _handleClientData(int clientFd);
    void _disconnectClient(int clientFd);
    void _processMessage(Client* client, const std::string& message);

    void _queueForSend(int clientFd, const std::string& message);
    void _flushOutgoing(int clientFd);
    void _setPollout(int clientFd, bool enable);
    void _scheduleDisconnect(int clientFd);
    bool _hasPendingWrites() const;
    void _flushWritableClients();
    void _broadcastToChannel(Channel* channel, const std::string& message, int exceptFd);

    std::string _clientPrefix(Client* client) const;
    void _sendNumeric(Client* client, const std::string& line);
    void _tryRegisterClient(Client* client);
    void _removeEmptyChannels();
    void _joinChannel(Client* client, Channel* channel, const std::string& channelName);

    Client* _findClientByNick(const std::string& nick);
    int _findFdByNick(const std::string& nick);
    Channel* _getChannel(const std::string& name);

    void _handlePass(Client* client, const std::string& params);
    void _handleNick(Client* client, const std::string& params);
    void _handleUser(Client* client, const std::string& params);
    void _handleJoin(Client* client, const std::string& params);
    void _handlePart(Client* client, const std::string& params);
    void _handlePrivmsg(Client* client, const std::string& params);
    void _handleQuit(Client* client, const std::string& params);
    void _handleTopic(Client* client, const std::string& params);
    void _handleKick(Client* client, const std::string& params);
    void _handleInvite(Client* client, const std::string& params);
    void _handleMode(Client* client, const std::string& params);
    void _handlePing(Client* client, const std::string& params);
    void _handleCap(Client* client, const std::string& params);
    void _handleBotPrivmsg(Client* client, const std::string& target, const std::string& text);

public:
    Server(int port, const std::string& password);
    ~Server();

    void start();
};

#endif
