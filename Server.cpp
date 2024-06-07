#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <winsock2.h>
#include <ws2tcpip.h>

// 定义最大缓冲区大小
#define MAX_BUFFER_SIZE 4096

// 用户结构体，存储用户名和对应的套接字
struct User {
    std::string username;
    SOCKET socket{};
};

// 消息结构体，存储消息发送者、接收者和内容
struct Message {
    std::string from;
    std::vector<std::string> to;
    std::string content;
};

// 重叠结构体
struct OverlappedEx {
    WSAOVERLAPPED overlapped;
    WSABUF wsabuf;
    char buffer[MAX_BUFFER_SIZE];
    int operation;  // 用于区分操作类型，例如接收或发送
};

// 发送消息给指定套接字
void sendMessage(SOCKET socket, const std::string &message);

// 全局用户映射，键为用户名，值为User结构体
std::map<std::string, User> users;  // key: username, value: User
// 用于保护用户映射的互斥锁
std::mutex usersMutex;

/**
 * 创建一个新的OverlappedEx结构体
 * 使用智能指针管理OverlappedEx的生命周期
 * @return 新创建的OverlappedEx指针
 */
std::unique_ptr<OverlappedEx> createOverlappedEx() {
    auto overlappedEx = std::make_unique<OverlappedEx>();
    ZeroMemory(&(overlappedEx->overlapped), sizeof(WSAOVERLAPPED));
    overlappedEx->wsabuf.buf = overlappedEx->buffer;
    overlappedEx->wsabuf.len = MAX_BUFFER_SIZE;
    overlappedEx->operation = 1;  // 设置为接收操作
    return overlappedEx;
}

/**
 * 创建一个新的套接字并将其与完成端口关联
 * @param completionPort 完成端口句柄
 * @return 新创建的套接字，错误时返回INVALID_SOCKET
 */
SOCKET createSocketAndAssociateWithCompletionPort(HANDLE completionPort) {
    SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Error at WSASocket(): " << WSAGetLastError() << std::endl;
        return INVALID_SOCKET;
    }

    if (CreateIoCompletionPort((HANDLE) clientSocket, completionPort, (ULONG_PTR) clientSocket, 0) == nullptr) {
        std::cerr << "Error in associating socket with completion port: " << GetLastError() << std::endl;
        closesocket(clientSocket);
        return INVALID_SOCKET;
    }
    return clientSocket;
}

/**
 * 解析接收到的数据并根据命令执行相应操作
 * @param overlappedEx 接收操作对应的过重叠结构体
 * @param completionKey 完成端口的关联值
 */
void parseAndHandleMessage(OverlappedEx *overlappedEx, ULONG_PTR completionKey) {
    std::string data(overlappedEx->buffer);
    std::istringstream stream(data);
    std::string command;
    stream >> command;

    if (command == "REGISTER") {
        std::string username;
        stream >> username;
        {
            std::lock_guard<std::mutex> lock(usersMutex);
            users[username] = {username, (SOCKET) completionKey};
        }
        std::cout << "New user registered: " << username << std::endl;
    } else if (command == "MESSAGE") {
        std::string username, message;
        stream >> username;
        getline(stream, message);  // 获取剩余部分作为消息
        std::lock_guard<std::mutex> lock(usersMutex);
        if (users.find(username) != users.end()) {
            sendMessage(users[username].socket, message);
        }
    }
}

// 发送消息函数
void sendMessage(SOCKET socket, const std::string &message) {
    auto sendOverlappedEx = createOverlappedEx();
    memcpy(sendOverlappedEx->buffer, message.c_str(), message.length());
    sendOverlappedEx->wsabuf.len = message.length();
    sendOverlappedEx->operation = 2;  // 设置为发送操作
    DWORD sentBytes = 0;
    int result = WSASend(socket, &(sendOverlappedEx->wsabuf), 1, &sentBytes, 0, &(sendOverlappedEx->overlapped), nullptr);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        std::cerr << "WSASend failed: " << WSAGetLastError() << std::endl;
    }
}

/**
 * 初始化WinSock库
 * @return 初始化结果，0表示成功
 */
int initializeWinSock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return result;
    }
    return 0;
}

/**
 * 创建服务器套接字并绑定到指定端口
 * @param port 服务器端口
 * @return 创建的服务器套接字，错误时返回INVALID_SOCKET
 */
SOCKET createServerSocket(const char *port) {
    struct addrinfo hints{}, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(nullptr, port, &hints, &result) != 0) {
        std::cerr << "getaddrinfo failed." << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    SOCKET listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (bind(listenSocket, result->ai_addr, (int) result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Bind failed with error: " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        closesocket(listenSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }

    freeaddrinfo(result);
    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return INVALID_SOCKET;
    }
    return listenSocket;
}

/**
 * 创建完成端口并将其与服务器套接字关联
 * @param listenSocket 服务器套接字
 * @param numberOfConcurrentThreads 完成端口可处理的并发线程数
 * @return 创建的完成端口句柄，错误时返回nullptr
 */
HANDLE createCompletionPort(SOCKET listenSocket, int numberOfConcurrentThreads) {
    HANDLE completionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, numberOfConcurrentThreads);
    if (completionPort == nullptr) {
        std::cerr << "CreateIoCompletionPort failed: " << GetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return nullptr;
    }

    if (CreateIoCompletionPort((HANDLE) listenSocket, completionPort, (ULONG_PTR) 0, 0) == nullptr) {
        std::cerr << "Error in associating socket with completion port: " << GetLastError() << std::endl;
        closesocket(listenSocket);
        CloseHandle(completionPort);
        WSACleanup();
        return nullptr;
    }
    return completionPort;
}

/**
 * 服务器主循环，处理完成端口上的事件
 * @param completionPort 服务器的完成端口句柄
 */
[[noreturn]] void serverLoop(HANDLE completionPort) {
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    OverlappedEx *overlappedEx;
    BOOL result;

    while (true) {
        result = GetQueuedCompletionStatus(completionPort, &bytesTransferred, &completionKey,
                                           (LPOVERLAPPED *) &overlappedEx, INFINITE);
        if (result == FALSE) {
            std::cerr << "GetQueuedCompletionStatus failed: " << GetLastError() << std::endl;
            continue;
        }

        if (overlappedEx == nullptr) {
            continue;
        }

        if (overlappedEx->operation == 1) { // 假设1表示接收
            if (bytesTransferred > 0) {
                overlappedEx->buffer[bytesTransferred] = '\0'; // 确保字符串终止
                parseAndHandleMessage(overlappedEx, completionKey);
            }
        }

        // 重新投递接收请求
        ZeroMemory(&(overlappedEx->overlapped), sizeof(WSAOVERLAPPED));
        overlappedEx->wsabuf.len = MAX_BUFFER_SIZE;
        DWORD flags = 0;
        int WSARecvResult = WSARecv((SOCKET) completionKey, &(overlappedEx->wsabuf), 1, &bytesTransferred, &flags,
                             &(overlappedEx->overlapped), nullptr);
        if (WSARecvResult == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            std::cerr << "WSARecv failed: " << WSAGetLastError() << std::endl;
        }
    }
}

int main() {
    int result = initializeWinSock();
    if (result != 0) {
        return 1;
    }

    SOCKET serverSocket = createServerSocket("12345");
    if (serverSocket == INVALID_SOCKET) {
        return 1;
    }

    HANDLE completionPort = createCompletionPort(serverSocket, 0);
    if (completionPort == nullptr) {
        return 1;
    }

    serverLoop(completionPort);

    // 清理资源
    closesocket(serverSocket);
    CloseHandle(completionPort);
    WSACleanup();
    return 0;
}
