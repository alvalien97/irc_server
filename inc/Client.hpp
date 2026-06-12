#ifndef CLIENT_HPP
#define CLIENT_HPP

#include <string>

class Client {
private:
    int _fd;
    std::string _nickname;
    std::string _username;
    std::string _realname;
    std::string _buffer;
    bool _isAuthenticated;
    bool _hasUser;
    bool _hasPassed;

public:
    Client(int fd);
    ~Client();

    int getFd() const;
    void appendBuffer(const std::string& data);
    bool hasCompleteMessage() const;
    std::string extractMessage();

    const std::string& getNickname() const;
    const std::string& getUsername() const;
    const std::string& getRealname() const;
    bool hasNickname() const;
    bool hasUser() const;
    bool isAuthenticated() const;
    bool hasPassed() const;
    bool isReadyForWelcome() const;

    void setNickname(const std::string& nickname);
    void setUsername(const std::string& username);
    void setRealname(const std::string& realname);
    void setPassed();
    void setUserDone();
    void setAuthenticated();
};

#endif
