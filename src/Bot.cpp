#include "Bot.hpp"
#include "Client.hpp"

const std::string Bot::NICK = "ft_bot";
const std::string Bot::USER = "bot";

bool Bot::isBotNick(const std::string& elbot) {
    return elbot == NICK;
}

bool Bot::isBotCommand(const std::string& text) {
    return !text.empty() && text[0] == '!';
}

std::string Bot::buildReply(const std::string& elbot, const std::string& text) {
    return ":" + NICK + "!" + USER + "@127.0.0.1 PRIVMSG " + elbot + " :" + text; //protocolo IRC (RCF 2812)
}

std::string Bot::handleMessage(Client* sender, const std::string& text) {
    std::string cmd = text;
    if (!cmd.empty() && cmd[0] == '!')
        cmd = cmd.substr(1);

    if (cmd == "help" || cmd == "h") {
        return "Commands: !help !ping !time !info — or PRIVMSG " + NICK + " directly";
    }
    if (cmd == "ping") {
        return "pong " + sender->getNickname();
    }
    if (cmd == "time") {
        return "Server time: use date in your shell; ft_bot runs inside ircserv";
    }
    if (cmd == "info") {
        return "ft_irc bonus bot by alvalien/isrguer — 42 ft_irc project";
    }
    return "Unknown command. Try !help";
}
