#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <Winsock2.h>

#pragma comment(lib, "WS2_32.lib")

constexpr size_t BUF_SIZE = 4096;
std::atomic<bool> continueReceiving{true};

void receiveMessages(SOCKET sHost) {
    std::vector<char> buf(BUF_SIZE, 0);
    int retVal;

    while (continueReceiving) {
        retVal = recv(sHost, buf.data(), BUF_SIZE - 1, 0);
        if (retVal > 0) {
            buf[retVal] = '\0';
            std::cout << buf.data() << std::endl;
        } else if (retVal == 0) {
            std::cout << "Connection closed by the server." << std::endl;
            break;
        } else {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                std::cerr << "recv failed with error: " << err << std::endl;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

int initializeSocket(SOCKET &sHost, const std::string &ip, const std::string &port) {
    WSADATA wsd;
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return -1;
    }

    sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sHost == INVALID_SOCKET) {
        std::cerr << "socket creation failed!" << std::endl;
        WSACleanup();
        return -1;
    }

    SOCKADDR_IN servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    servAddr.sin_port = htons(static_cast<u_short>(std::stoi(port)));

    if (connect(sHost, reinterpret_cast<LPSOCKADDR>(&servAddr), sizeof(servAddr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed!" << std::endl;
        closesocket(sHost);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server successfully." << std::endl;
    return 0;
}

void cleanup(SOCKET sHost) {
    closesocket(sHost);
    WSACleanup();
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Server IP> <Port>" << std::endl;
        return 1;
    }

    SOCKET sHost;
    if (initializeSocket(sHost, argv[1], argv[2]) != 0) {
        return 1;
    }

    std::string username;
    std::cout << "Enter your username: ";
    std::cin >> username;
    std::string registerMessage = "REGISTER SERVER " + username;
    send(sHost, registerMessage.c_str(), (int) registerMessage.size(), 0);

    std::thread receiverThread(receiveMessages, sHost);

    std::string input;
    std::cin.ignore();  // 忽略之前输入的换行符
    std::cout << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::cout << std::endl;
    std::cout << "Enter message or 'exit' to quit:" << std::endl;
    while (true) {
        std::getline(std::cin, input);
        if (input == "exit") {
            continueReceiving = false;  // 告诉接收线程停止接收消息
            std::string removeMessage = "REMOVE SERVER " + username;
            send(sHost, removeMessage.c_str(), (int) removeMessage.size(), 0);  // 发送移除用户的消息
            break;
        }
        else if (input.substr(0, 6) == "create") {
            std::string groupName = input.substr(input.find(' ') + 1); // 获取第一个空格后的部分
            std::string createGroupMessage = "CREATE_GROUP " + groupName;
            send(sHost, createGroupMessage.c_str(), (int) createGroupMessage.size(), 0);
        }
        else if (input.substr(0, 5) == "group") {
            std::string groupName = input.substr(0, input.find(' ')); // 获取第一个空格前的部分
            std::string message = input.substr(input.find(' '),input.size());
            std::string groupMessage = "GROUP_MESSAGE " + groupName + " " + message;
            send(sHost, groupMessage.c_str(), (int) groupMessage.size(), 0);
        }
        else if (input.substr(0, 5) == "check") {
            std::string groupName = input.substr(input.find(' ')+1); // 获取第一个空格前的部分
            std::string groupMessage = "GROUP_CHECK " + groupName;
            send(sHost, groupMessage.c_str(), (int) groupMessage.size(), 0);
        }
        else if (input.substr(0, 4) == "join") {
            std::string groupName = input; // 假设输入格式为 "join group_name"
            std::string joinGroupMessage = "JOIN_GROUP " + groupName;
            send(sHost, joinGroupMessage.c_str(), (int) joinGroupMessage.size(), 0);
        }
        else {
            std::string message = "MESSAGE " + input;
            send(sHost, message.c_str(), (int) message.size(), 0);
        }
    }

    receiverThread.join();
    cleanup(sHost);
    return 0;
}
