#include <iostream>
#include <string>
#include <thread>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

void receiveMessages(int sock) {
    char buffer[1024];
    while (true) {
        memset(buffer, 0, sizeof(buffer));
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) {
            cout << "Disconnected from server." << endl;
            close(sock);
            exit(0);
        }
        cout << buffer << endl;
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2 && argc != 3) {
        cerr << "Usage: ./client [server_ip] <port>" << endl;
        cerr << "Example (local): ./client 8080" << endl;
        cerr << "Example (remote): ./client 192.168.1.105 8080" << endl;
        return 1;
    }

    string serverIp = "127.0.0.1";
    int port;

    if (argc == 2) {
        port = stoi(argv[1]);
    } else {
        serverIp = argv[1];
        port = stoi(argv[2]);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Failed to create socket." << endl;
        return 1;
    }

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp.c_str(), &serverAddr.sin_addr);

    if (connect(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Connection failed to " << serverIp << ":" << port << endl;
        return 1;
    }

    string username;
    cout << "Enter your username: ";
    getline(cin, username);
    if (username.empty()) username = "Anonymous";
    send(sock, username.c_str(), username.size(), 0);

    cout << "\n╔════════════════════════════════════════════════╗" << endl;
    cout << "║     Connected to OPTICOM Chat Server!         ║" << endl;
    cout << "╚════════════════════════════════════════════════╝" << endl;
    cout << "\n💡 Type /help to see available commands" << endl;
    cout << "💡 Type /quit to disconnect\n" << endl;

    thread receiver(receiveMessages, sock);
    receiver.detach();

    string message;
    while (true) {
        getline(cin, message);

        if (message == "/quit") {
            cout << "Disconnecting..." << endl;
            close(sock);
            exit(0);
        }

        if (message.empty()) continue;

        ssize_t sent = send(sock, message.c_str(), message.size(), 0);
        if (sent < 0) {
            cerr << "Send failed." << endl;
            close(sock);
            exit(1);
        }
    }

    return 0;
}
