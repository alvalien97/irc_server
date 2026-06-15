#include "Server.hpp"
#include "Bot.hpp"
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>

const std::string Server::SERVER_NAME = "ft_irc.local";

Server::Server(int port, const std::string& password)//inicio server
    : _port(port), _password(password), _serverSocket(-1) {
    std::cout << "Server listening on port " << _port << std::endl;
}

Server::~Server() {//destructor (memoria dinamica con mapeado)
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

void Server::_initSocket() {//Creo socket de la red, oido del server.
    _serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverSocket < 0)
        throw std::runtime_error("Failed to create socket");

    int opt = 1;
    if (setsockopt(_serverSocket, SOL_SOCKET, SO_REUSEADDR/*reinicio server sin esperar liberar el puerto*/, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("Failed to setsockopt");

    if (fcntl(_serverSocket, F_SETFL, O_NONBLOCK) < 0)//evito que el server se bloquee
        throw std::runtime_error("Failed to set non-blocking on server socket");

    struct sockaddr_in serverAddr;
    std::memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(_port);

    if (bind(_serverSocket, reinterpret_cast<struct sockaddr*>(&serverAddr), sizeof(serverAddr)) < 0)
        throw std::runtime_error("Failed to bind socket to port");//bind a la ip/puerto

    if (listen(_serverSocket, SOMAXCONN) < 0)
        throw std::runtime_error("Failed to listen on socket");

    struct pollfd pfd;
    pfd.fd = _serverSocket;
    pfd.events = POLLIN;
    pfd.revents = 0;
    _pollFds.push_back(pfd);

    std::cout << "Socket bound and listening." << std::endl;
}

void Server::_setPollout(int clientFd, bool enable) {//avisame si el buffer esta libre para escribir
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

void Server::_queueForSend(int clientFd, const std::string& message) {//en vez de hacer un send que podria bloquear el servidor, añade el texto de buffer a la salida del cliente y aseguro que acabe en \n o \r
    std::string line = message;
    if (line.size() < 2 || line.substr(line.size() - 2) != "\r\n")
        line += "\r\n";
    _outBuffers[clientFd] += line;
    _setPollout(clientFd, true);
}

void Server::_flushOutgoing(int clientFd) {//llamo a send, si logro enviar todo apaga el pollout, si no borro lo enviado y dejo el resto para la proxima vez. Si el cliente estaba pending y el buffer se vació lo desconecta
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

bool Server::_hasPendingWrites() const {//reviso que no hay bytes pendientes
    for (std::map<int, std::string>::const_iterator it = _outBuffers.begin(); it != _outBuffers.end(); ++it) {
        if (!it->second.empty())
            return true;
    }
    return false;
}

void Server::_flushWritableClients() {//cada vez que start termina si hay una minipoll ejecuta flushoutgoing de forma segura
    if (!_hasPendingWrites())
        return;

    //Actualizamos los revents llamando a poll con timeout 0
    poll(&_pollFds[0], _pollFds.size(), 0);

    //Recojo los FDs que tienen POLLOUT en un vector temporal
    std::vector<int> fdsToFlush;
    for (size_t i = 0; i < _pollFds.size(); ++i) {
        if (_pollFds[i].revents & POLLOUT) {
            fdsToFlush.push_back(_pollFds[i].fd);
        }
    }

    //Iteramos de forma completamente segura sobre los FDs recolectados
    for (size_t i = 0; i < fdsToFlush.size(); ++i) {
        int fd = fdsToFlush[i];
        
        // SEGURIDAD CRÍTICA: Verificamos si el cliente todavía existe en el servidor.
        // Si una iteración previa desconectó a este usuario por daño colateral,
        // simplemente lo ignoramos.
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
    //envia mensaje a todos los users de un canal
    for (std::map<int, Client*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
        if (channel->hasClient(it->first) && it->first != exceptFd)
            _queueForSend(it->first, message);
    }
}

std::string Server::_clientPrefix(Client* client) const {
    //construye mascara estandar de irc
    return ":" + client->getNickname() + "!" + client->getUsername() + "@127.0.0.1";
}

void Server::_sendNumeric(Client* client, const std::string& line) {
    //automatiza envio de respuestas oficiales del servidor, anteponiendo nombre de tu servidor
    _queueForSend(client->getFd(), ":" + SERVER_NAME + " " + line);
}

Client* Server::_findClientByNick(const std::string& nick) {
    //buscador que recorre el mapa de clientes y empareja nick con su puntero o num de socket
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
    //existe este canal con este nombre?
    std::map<std::string, Channel*>::iterator it = _channels.find(name);
    if (it == _channels.end())
        return 0;
    return it->second;
}

void Server::_removeEmptyChannels() {
    //revisa no haya canales fantasma flotando
    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ) {
        if (it->second->getClientCount() == 0) {
            delete it->second;
            _channels.erase(it++);
        } else {
            ++it;
        }
    }
}

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

void Server::_acceptNewClient() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int clientFd = accept(_serverSocket, reinterpret_cast<struct sockaddr*>(&clientAddr), &clientLen);

    if (clientFd < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)//evito errores no bloqueantes
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

void Server::_disconnectClient(int clientFd) {
    Client* client = 0;
    if (_clients.find(clientFd) != _clients.end())
        client = _clients[clientFd];

    if (client && client->hasNickname()) {
        std::string quitMsg = _clientPrefix(client) + " QUIT :Client disconnected";//avisa a todos los cliente que compartian canal con el
        for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
            if (it->second->hasClient(clientFd))
                _broadcastToChannel(it->second, quitMsg, clientFd);
        }
    }

    for (std::map<std::string, Channel*>::iterator it = _channels.begin(); it != _channels.end(); ++it) {
        if (it->second->hasClient(clientFd))
            it->second->removeClient(clientFd);
    }
    _removeEmptyChannels();//borro canal si se queda vacio

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

void Server::_handleClientData(int clientFd) {
    char buf[512];
    std::memset(buf, 0, sizeof(buf));

    int bytesRead = recv(clientFd, buf, sizeof(buf) - 1, 0);//leo hasta 511 bytes y lo concateno al buffer del cliente
    if (bytesRead <= 0) {
        _disconnectClient(clientFd);
        return;
    }

    // Buscamos si el cliente existe de forma segura
    std::map<int, Client*>::iterator it = _clients.find(clientFd);
    if (it == _clients.end())
        return;

    Client* client = it->second;
    client->appendBuffer(std::string(buf, bytesRead));

    // Controlamos en cada iteración que el FD siga registrado en el mapa
    while (_clients.find(clientFd) != _clients.end() && _clients[clientFd]->hasCompleteMessage()) {
        std::string msg = _clients[clientFd]->extractMessage();//extrae comandos hasta \r o \n
        _processMessage(_clients[clientFd], msg);
    }
}

void Server::_joinChannel(Client* client, Channel* channel, const std::string& channelName) {
    //Introduce al usuario formalmente en el canal, le manda eco del JOIN
    //topic es 332, 353 y 366 son usuarios conectados
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
    // Solución al cambio de nick en tiempo real:
    // Si el usuario ya está autenticado, notificamos el cambio a todo su entorno
    if (client->isAuthenticated()) {
        std::string oldNick = client->getNickname();
        std::string nickMsg = _clientPrefix(client) + " NICK " + params;
        
        // Buscamos todos los sockets únicos que deben enterarse (él mismo + compañeros de canal)
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

void Server::_handleJoin(Client* client, const std::string& params) {
    //canal no existe lo crea, vrifico reestricciones como +i si es por
    //invitacion, +l por limite de usuarios, +k por contraseña
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
    //saca usuario del canal
    if (params.empty()) {
        _sendNumeric(client, "461 PART :Not enough parameters");
        return;
    }

    std::string channelName;
    std::string reason = "";
    
    // Parsear el nombre del canal y la razón opcional
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

    // Buscar si el canal existe en el servidor
    Channel* channel = _getChannel(channelName);
    if (!channel) {
        _sendNumeric(client, "403 " + client->getNickname() + " " + channelName + " :No such channel");
        return;
    }

    // Verificar si el cliente realmente forma parte de ese canal
    if (!channel->hasClient(client->getFd())) {
        _sendNumeric(client, "442 " + client->getNickname() + " " + channelName + " :You're not on that channel");
        return;
    }

    // Construir el mensaje oficial de PART
    // Nota: Asumiendo que tu función _clientPrefix devuelve "nick!user@host" sin los dos puntos iniciales
    std::string partMsg = _clientPrefix(client) + " PART " + channelName;
    if (!reason.empty()) {
        partMsg += " :" + reason;
    }
    partMsg += "\r\n";

    // ¡PASO CLAVE! Enviamos el mensaje a todos los miembros del canal.
    // Pasamos -1 como exceptFd para asegurar que el cliente que se va TAMBIÉN reciba el mensaje.
    _broadcastToChannel(channel, partMsg, -1);

    // Ahora que el cliente ya tiene el eco de confirmación en su buffer de red, lo borramos del canal
    channel->removeClient(client->getFd());

    // Limpieza: si el canal se quedó vacío, lo destruimos del servidor
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
        //_queueForSend(client->getFd(), fullMsg); //omito esta linea y quito el duplicado
    } else {
        int targetFd = _findFdByNick(target);
        if (targetFd == -1) {
            _sendNumeric(client, "401 " + target + " :No such nick/channel");
            return;
        }
        _queueForSend(targetFd, fullMsg);//el usuario no ve el menaje si no esta en el chat
    }
}

void Server::_handleQuit(Client* client, const std::string& params) {
    //procesa alida con el comando quit
    
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

void Server::_handlePing(Client* client, const std::string& params) {
    if (params.empty())
        _queueForSend(client->getFd(), "PONG :" + SERVER_NAME);
    else
        _queueForSend(client->getFd(), "PONG " + SERVER_NAME + " :" + params);
}

void Server::_handleCap(Client* client, const std::string& params) {//engaño al cliente para decirle qe no soporto extensiones modernas
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
    //separa el comando de los parametros convirtiendolo a mayusculas
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

void Server::start() {
    _initSocket();
    std::cout << "Waiting for connections..." << std::endl;
    //poll para monitorear eventos de la red
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
                    _scheduleDisconnect(fd);// si no hay nada en el buffer del cliente lo echa, y si no lo mete en _pendinClose
            } else {
                if (revents & POLLIN) {//pollin acepta nuevo cliente
                    if (fd == _serverSocket)
                        _acceptNewClient();
                    else
                        _handleClientData(fd);
                }

                if (revents & POLLOUT)//si un cliente tiene pollout lee manda datos pendientes
                    _flushOutgoing(fd);

                if ((revents & POLLHUP) && fd != _serverSocket)
                    _scheduleDisconnect(fd);
            }

            // Si el tamaño del vector disminuyó, significa que el cliente actual fue borrado
            // de _pollFds. Los elementos se desplazaron a la izquierda, por lo que el siguiente
            // elemento ahora ocupa la posición 'i'. ¡NO debemos incrementar 'i'!
            if (_pollFds.size() < oldSize) {
                continue;
            }
            ++i;
        }

        _flushWritableClients();
    }
}
