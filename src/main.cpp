#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <signal.h>

// Variable global para apagar el servidor limpiamente desde la señal
bool g_serverRunning = true;

void handleSignal(int signum) {
    (void)signum;//numero de la señal recibida
    g_serverRunning = false;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: ./ircserv <port> <password>\n";
        return 1;
    }

    int port = std::atoi(argv[1]);
    std::string password = argv[2];

    // Capturamos Ctrl+C (SIGINT) y SIGTERM
    signal(SIGINT, handleSignal);//ctrl c y ctrl d
    signal(SIGTERM, handleSignal);

    try {
        Server server(port, password);
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << '\n';
        return 1;
    }

    std::cout << "\nSERVER SHUTED DOWN, WELLCOME TO MILLESTONE 6!" << std::endl;
    return 0;
}
