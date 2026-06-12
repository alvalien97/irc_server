#ifndef BOT_HPP
#define BOT_HPP

#include <string>

class Client;

class Bot {
public:
    static const std::string NICK;
    static const std::string USER;

    static bool isBotNick(const std::string& target);
    static bool isBotCommand(const std::string& text);
    static std::string buildReply(const std::string& target, const std::string& text);
    static std::string handleMessage(Client* sender, const std::string& text);
};

#endif
