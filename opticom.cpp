#include <iostream>
#include <string>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <csignal>
#include <cstdlib>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

using namespace std;

bool serverRunning = true;

// Prints a clean help menu
void print_help() {
    cout << "Opticom Chat Server\n"
         << "Usage: ./opticom [OPTIONS]\n\n"
         << "Options:\n"
         << "  --help, -h         Show this help menu\n"
         << "  -p, --port <num>   Start server on specified port (default: 8080)\n";
}

void signalHandler(int sig) {
    serverRunning = false;
}

string nowTimestamp() {
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);

    char buffer[20];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &t);
    return string(buffer);
}

struct ClientInfo {
    int socket;
    string name;
    string ip;
    string room;
};

class ChatServer {
private:
    int serverSocket;
    bool running;
    vector<ClientInfo> clients;
    unordered_map<string, vector<string>> room_history;
    mutex clientsMutex;
    const size_t MAX_HISTORY = 50;

public:
    ChatServer() : serverSocket(-1), running(false) {}

    ~ChatServer() {
        stop();
    }

    void start(int port) {
        running = true;

        // Ignore SIGPIPE to avoid server crash when sending to closed socket
        signal(SIGPIPE, SIG_IGN);

        serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (serverSocket < 0) {
            cerr << "Socket creation failed\n";
            return;
        }

        int opt = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        serverAddr.sin_addr.s_addr = INADDR_ANY;

        if (bind(serverSocket, (sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
            cerr << "Bind failed\n";
            close(serverSocket);
            return;
        }

        if (listen(serverSocket, 10) < 0) {
            cerr << "Listen failed\n";
            close(serverSocket);
            return;
        }

        cout << "Server started on port " << port << endl;

        acceptClients();
    }

    void stop() {
        running = false;

        if (serverSocket >= 0) {
            close(serverSocket);
            serverSocket = -1;
        }

        lock_guard<mutex> lock(clientsMutex);
        for (auto &c : clients) {
            close(c.socket);
        }
        clients.clear();
    }

private:

    void acceptClients() {
        while (running && serverRunning) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);

            int clientSocket = accept(serverSocket, (sockaddr *)&clientAddr, &clientLen);
            if (clientSocket < 0) continue;

            string ip = inet_ntoa(clientAddr.sin_addr);
            cout << "New connection from " << ip << endl;

            thread(&ChatServer::handleClient, this, clientSocket, ip).detach();
        }
    }

    void handleClient(int clientSocket, const string &ip) {
        char buffer[1024];

        // Ask for a name
        send(clientSocket, "Enter your name: ", 17, 0);
        memset(buffer, 0, sizeof(buffer));
        recv(clientSocket, buffer, sizeof(buffer), 0);
        string name = string(buffer);
        name.erase(remove(name.begin(), name.end(), '\n'), name.end());

        // Assign to default room "general"
        string room = "general";

        {
            lock_guard<mutex> lock(clientsMutex);
            clients.push_back({clientSocket, name, ip, room});
        }

        send(clientSocket, ("Joined room: " + room + "\n").c_str(),
             room.size() + 15, 0);

        // Send recent history
        sendRoomHistory(clientSocket, room);

        broadcastMessage(name + " joined the chat", clientSocket, room);

        while (running && serverRunning) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes = recv(clientSocket, buffer, sizeof(buffer), 0);

            if (bytes <= 0) break;

            string message(buffer);
            message.erase(remove(message.begin(), message.end(), '\n'), message.end());
            string fullMessage = "[" + nowTimestamp() + "] " + name + ": " + message;

            addToHistory(room, fullMessage);
            broadcastMessage(fullMessage, clientSocket, room);
        }

        removeClient(clientSocket);
        close(clientSocket);
        cout << name << " disconnected\n";
    }

    void sendRoomHistory(int clientSocket, const string &room) {
        if (room_history.count(room)) {
            for (const string &msg : room_history[room]) {
                send(clientSocket, msg.c_str(), msg.size(), 0);
                send(clientSocket, "\n", 1, 0);
            }
        }
    }

    void addToHistory(const string &room, const string &msg) {
        auto &history = room_history[room];
        if (history.size() >= MAX_HISTORY) {
            history.erase(history.begin());
        }
        history.push_back(msg);
    }

    // Improved: no blocking under mutex
    void broadcastMessage(const string &msg, int senderSocket, const string &room) {
        vector<int> targets;

        {
            lock_guard<mutex> lock(clientsMutex);
            for (auto &c : clients) {
                if (c.room == room && c.socket != senderSocket)
                    targets.push_back(c.socket);
            }
        }

        for (int sock : targets) {
            ssize_t sent = send(sock, msg.c_str(), msg.size(), 0);
            if (sent < 0) continue;
            send(sock, "\n", 1, 0);
        }
    }

    void removeClient(int socket) {
        lock_guard<mutex> lock(clientsMutex);
        clients.erase(remove_if(clients.begin(), clients.end(),
                                [&](const ClientInfo &c) {
                                    return c.socket == socket;
                                }),
                      clients.end());
    }
};

// MAIN FUNCTION
int main(int argc, char *argv[]) {

    // Handle Ctrl+C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // OPTION: --help
    if (argc > 1) {
        string a = argv[1];
        if (a == "--help" || a == "-h") {
            print_help();
            return 0;
        }
    }

    // Default port
    int port = 8080;

    // OPTION: -p or --port
    if (argc > 2) {
        string a = argv[1];
        if (a == "-p" || a == "--port") {
            port = atoi(argv[2]);
            if (port <= 0 || port > 65535) {
                cerr << "Invalid port. Using default 8080.\n";
                port = 8080;
            }
        }
    }

    ChatServer server;
    server.start(port);

    return 0;
}
