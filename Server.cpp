#include <iostream>
#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>
#include <unordered_map>
#include <string>

// 定义缓冲区大小
#define BUF_SIZE 4096

/**
 * 客户端信息结构体，包含客户端的ID、套接字、地址、重叠结构体和缓冲区
 */
struct ClientInfo {
    int id{};
    SOCKET sclient{};
    sockaddr_in addrClient{};
    OVERLAPPED overlapped{};
    char buf[BUF_SIZE]{};
    std::string username;  // 新增用户名字段
};

// 全局互斥锁，用于线程安全访问
std::mutex connectionMutex;
// 连接计数器，原子类型，用于线程安全操作
std::atomic<int> connectionCount(0);
// 下一个客户端ID，原子类型，用于线程安全操作
std::atomic<int> nextClientId(1);
// 客户端列表，存储客户端信息指针
std::vector<ClientInfo *> clients;
// 用户名到客户端信息的映射，用于快速查找
std::unordered_map<std::string, ClientInfo *> userMap;  // 用户名与ClientInfo的映射
//群组与ClientInfo的映射
std::unordered_map<std::string, std::vector<ClientInfo *>> groupMap;

// 函数声明
void ProcessClient(ClientInfo *clientInfo);

// 键盘输入线程函数，用于接收控制台输入并发送给所有客户端
DWORD WINAPI KeyboardThread(LPVOID);

// 清理函数，用于关闭套接字和释放资源
void Cleanup();

int main() {
    WSADATA wsaData;
    SOCKET sServer;
    int retVal;

    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed !" << std::endl;
        return 1;
    }

    // 创建服务器套接字
    sServer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sServer == INVALID_SOCKET) {
        std::cerr << "Socket failed !" << std::endl;
        WSACleanup();
        return -1;
    }

    // 设置服务器地址信息并绑定
    sockaddr_in addrServ{};
    addrServ.sin_family = AF_INET;
    int port = 9990;
    addrServ.sin_port = htons(port);
    addrServ.sin_addr.S_un.S_addr = INADDR_ANY;

    retVal = bind(sServer, (sockaddr *) &addrServ, sizeof(addrServ));
    if (retVal == SOCKET_ERROR) {
        std::cerr << "Bind failed !" << std::endl;
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 开始监听
    retVal = listen(sServer, SOMAXCONN);
    if (retVal == SOCKET_ERROR) {
        std::cerr << "Listen failed !" << std::endl;
        closesocket(sServer);
        WSACleanup();
        return -1;
    }

    // 创建键盘输入线程
    CreateThread(nullptr, 0, KeyboardThread, nullptr, 0, nullptr);

    std::cout << "Server is listening on port " << port << " ..." << std::endl;

    // 循环等待客户端连接
    while (true) {
        sockaddr_in addrClient{};
        int addrClientLen = sizeof(addrClient);
        SOCKET sClient = accept(sServer, (sockaddr *) &addrClient, &addrClientLen);
        if (sClient == INVALID_SOCKET) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                std::cerr << "Accept failed with error: " << err << std::endl;
                break;
            }
            Sleep(100);
            continue;
        }

        // 为新客户端分配内存
        auto *clientInfo = new ClientInfo;
        clientInfo->sclient = sClient;
        clientInfo->addrClient = addrClient;
        clientInfo->id = nextClientId++;
        ZeroMemory(&clientInfo->overlapped, sizeof(clientInfo->overlapped));

        // 创建线程处理客户端请求
        std::thread(ProcessClient, clientInfo).detach();
    }

    // 服务结束后的清理工作
    Cleanup();
    return 0;
}

/**
 * 处理客户端连接的函数，每个客户端连接都会创建一个线程调用此函数
 * @param clientInfo 客户端信息指针
 */
void ProcessClient(ClientInfo *clientInfo) {
    // 创建事件对象并关联到OVERLAPPED结构体
    clientInfo->overlapped.hEvent = WSACreateEvent();
    if (clientInfo->overlapped.hEvent == nullptr) {
        std::cerr << "WSACreateEvent failed with error: " << WSAGetLastError() << std::endl;
        delete clientInfo;
        return;
    }

    // 客户端连接信息
    {
        std::lock_guard<std::mutex> lock(connectionMutex);
        connectionCount++;
        std::cout << "Client [" << clientInfo->id << "] connected from "
                  << inet_ntoa(clientInfo->addrClient.sin_addr) << ":" << ntohs(clientInfo->addrClient.sin_port)
                  << std::endl;
        clients.push_back(clientInfo);
        std::cout << "Total connections: " << connectionCount.load() << std::endl;
    }

    // 进入通信循环
    while (true) {
        DWORD bytesReceived;
        DWORD flags = 0;
        WSABUF dataBuf;
        dataBuf.buf = clientInfo->buf;
        dataBuf.len = BUF_SIZE;

        int retVal = WSARecv(clientInfo->sclient, &dataBuf, 1, &bytesReceived, &flags, &clientInfo->overlapped,
                             nullptr);
        if (retVal == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSA_IO_PENDING) {
                std::cerr << "WSARecv failed with error: " << err << std::endl;
                break;
            }
        }

        // 等待接收完成
        DWORD waitRet = WSAWaitForMultipleEvents(1, &clientInfo->overlapped.hEvent, TRUE, INFINITE, FALSE);
        if (waitRet == WSA_WAIT_FAILED) {
            std::cerr << "WSAWaitForMultipleEvents failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        // 检查接收操作是否已完成
        WSAGetOverlappedResult(clientInfo->sclient, &clientInfo->overlapped, &bytesReceived, FALSE, &flags);

        // 处理接收到的数据
        if (bytesReceived > 0) {
            clientInfo->buf[bytesReceived] = '\0';
            std::cout << "Received from [" << clientInfo->id << "][" << inet_ntoa(clientInfo->addrClient.sin_addr)
                      << ":" << ntohs(clientInfo->addrClient.sin_port) << "]: " << clientInfo->buf << std::endl;

            // 解析消息并处理
            char *token;
            char *context = nullptr;
            token = strtok_s(clientInfo->buf, " ", &context);
            std::string cmd(token);

            // 注册命令处理
            if (cmd == "REGISTER") {
                token = strtok_s(nullptr, " ", &context); // 第二段
                if (strcmp(token, "SERVER") == 0) { // 验证是否为 "SERVER"
                    token = strtok_s(nullptr, " ", &context); // 用户名
                    if (token != nullptr) {
                        std::string username(token);
                        clientInfo->username = username;
                        {
                            std::lock_guard<std::mutex> lock(connectionMutex);
                            userMap[username] = clientInfo;
                            std::cout << "User registered: " << username << std::endl;
                            // 通知客户端注册成功
                            send(clientInfo->sclient, "Server: Registered.", 19, 0);
                            // 向所有在线用户发送在线用户列表
                            std::string userList = "Server: Online users: ";
                            for (const auto &pair: userMap) {
                                userList += pair.first + " ";
                            }
                            for (auto client: clients) {
                                send(client->sclient, userList.c_str(), (int) userList.length(), 0);
                            }
                        }
                    }
                } else {
                    std::cerr << "Invalid registration command format." << std::endl;
                    // 通知客户端注册失败
                    send(clientInfo->sclient, "Server: Invalid registration command format.", 41, 0);
                }
            } else if (cmd == "MESSAGE") {// 消息发送命令处理
                token = strtok_s(nullptr, " ", &context);
                std::string target(token);
                std::string message(context);
                if (target == "SERVER") {
                    std::cout << "Message to SERVER: " << message << std::endl;
                } else {
                    std::lock_guard<std::mutex> lock(connectionMutex);
                    if (userMap.find(target) != userMap.end()) {
                        //在消息前加上发送者的用户名
                        message = clientInfo->username + ": " + context;
                        send(userMap[target]->sclient, message.c_str(), (int) message.length(), 0);
                        // 通知客户端消息已发送
                        send(clientInfo->sclient, "Server: Message sent.", 21, 0);
                    } else {
                        std::cout << "User not found: " << target << std::endl;
                        // 通知客户端用户不存在
                        send(clientInfo->sclient, "Server: User not found.", 23, 0);
                    }
                }
            } else if (cmd == "CREATE_GROUP") {// 创建群组命令处理
                //创建群组
                token = strtok_s(nullptr, " ", &context);
                std::string groupName(token);
                //检查群组是否已存在
                if (groupMap.find(groupName) != groupMap.end()) {
                    std::cout << "Group already exists: " << groupName << std::endl;
                    // 通知客户端群组已存在
                    send(clientInfo->sclient, "Server: Group already exists.", 27, 0);
                } else {
                    //创建群xxx
                    groupMap[groupName] = std::vector<ClientInfo *>();
                    groupMap[groupName].push_back(clientInfo);
                    std::cout << "Group created: " << groupName << std::endl;
                    // 通知客户端群组创建成功
                    send(clientInfo->sclient, "Server: Group created.", 23, 0);
                    //广播所有用户群组数量和名字
                    std::string groupList = "Server: Group list: ";
                    for (const auto &pair: groupMap) {
                        groupList += pair.first + " ";
                    }
                    for (auto client: clients) {
                        send(client->sclient, groupList.c_str(), (int) groupList.length(), 0);
                    }
                }
            } else if (cmd == "JOIN_GROUP") {// 加入群组命令处理
                //加入群组
                token = strtok_s(nullptr, " ", &context);
                token = strtok_s(nullptr, " ", &context);
                std::string groupName(token);
                //检查群组是否存在
                if (groupMap.find(groupName) != groupMap.end()) {
                    //检查用户是否已在群组中
                    if (std::find(groupMap[groupName].begin(), groupMap[groupName].end(), clientInfo) !=
                        groupMap[groupName].end()) {
                        std::cout << "User already in group: " << groupName << std::endl;
                        // 通知客户端用户已在群组中
                        send(clientInfo->sclient, "Server: User already in group.", 27, 0);
                    } else {
                        groupMap[groupName].push_back(clientInfo);
                        std::cout << "User joined group: " << groupName << std::endl;
                        // 通知客户端加入群组成功
                        send(clientInfo->sclient, "Server: Joined group.", 21, 0);
                    }
                } else {
                    std::cout << "Group not found: " << groupName << std::endl;
                    // 通知客户端群组不存在
                    send(clientInfo->sclient, "Server: Group not found.", 23, 0);
                }
            } else if (cmd == "GROUP_CHECK") {// 检查群组成员命令处理
                //查看群组成员
                token = strtok_s(nullptr, " ", &context);
                std::cout << token << std::endl;
                std::string groupName(token);
                //查看群组是否存在并输出成员名称
                if (groupMap.find(groupName) != groupMap.end()) {
                    std::string groupMembers = "Server: Group members: ";
                    for (auto member: groupMap[groupName]) {
                        groupMembers += member->username + " ";
                    }
                    send(clientInfo->sclient, groupMembers.c_str(), (int) groupMembers.length(), 0);
                } else {
                    std::cout << "Group not found: " << groupName << std::endl;
                    // 通知客户端群组不存在
                    send(clientInfo->sclient, "Server: Group not found.", 23, 0);
                }
            } else if (cmd == "GROUP_MESSAGE") {// 群组消息发送命令处理
                //群组消息
                token = strtok_s(nullptr, " ", &context);
                token = strtok_s(nullptr, " ", &context);
                std::cout << token << std::endl;
                std::string groupName(token);
                token = strtok_s(nullptr, " ", &context);
                std::cout << token << std::endl;
                std::string message(token);
                //检查群组是否存在
                if (groupMap.find(groupName) != groupMap.end()) {
                    //检查用户是否在群组中
                    if (std::find(groupMap[groupName].begin(), groupMap[groupName].end(), clientInfo) !=
                        groupMap[groupName].end()) {
                        //在消息前加上发送者的用户名
                        message = "(" + groupName + ") " + clientInfo->username + ": " + message;
                        for (auto member: groupMap[groupName]) {
                            send(member->sclient, message.c_str(), (int) message.length(), 0);
                        }
                        // 通知客户端消息已发送
                        send(clientInfo->sclient, "Server: Group message sent.", 27, 0);
                    }
                } else {
                    std::cout << "Group not found: " << groupName << std::endl;
                    // 通知客户端群组不存在
                    send(clientInfo->sclient, "Server: Group not found.", 23, 0);
                }
            } else if (cmd == "REMOVE") {// 移除用户命令处理
                {
                    std::lock_guard<std::mutex> lock(connectionMutex);
                    userMap.erase(clientInfo->username);
                    std::cout << "User removed: " << clientInfo->username << std::endl;
                    // 向所有在线用户发送在线用户列表
                    std::string userList = "Server: Online users: ";
                    for (const auto &pair: userMap) {
                        userList += pair.first + " ";
                    }
                    for (auto client: clients) {
                        send(client->sclient, userList.c_str(), (int) userList.length(), 0);
                    }
                }
            }
        } else {
            // 客户端断开连接
            std::cout << "Client [" << clientInfo->id << "] disconnected." << std::endl;
            {
                std::lock_guard<std::mutex> lock(connectionMutex);
                connectionCount--;
                clients.erase(std::remove(clients.begin(), clients.end(), clientInfo), clients.end());
                userMap.erase(clientInfo->username);
                std::cout << "Total connections: " << connectionCount.load() << std::endl;
                // 向所有在线用户发送在线用户列表
                std::string userList = "Server: Online users: ";
                for (const auto &pair: userMap) {
                    userList += pair.first + " ";
                }
                for (auto client: clients) {
                    send(client->sclient, userList.c_str(), (int) userList.length(), 0);
                }
            }
            break;
        }

        // 重置事件对象，准备下一次接收
        WSAResetEvent(clientInfo->overlapped.hEvent);
    }

    // 关闭事件对象
    WSACloseEvent(clientInfo->overlapped.hEvent);

    // 关闭套接字并清理内存
    closesocket(clientInfo->sclient);

    delete clientInfo;
}
// 键盘输入线程实现，允许服务器通过控制台向所有客户端发送消息
DWORD WINAPI KeyboardThread(LPVOID) {
    char input[BUF_SIZE];
    while (true) {
        std::cin.getline(input, BUF_SIZE);
        if (strcmp(input, "exit") == 0) break;

        std::lock_guard<std::mutex> lock(connectionMutex);
        for (auto client: clients) {
            std::cout << "Sending to [" << client->id << "]: " << input << std::endl;
            send(client->sclient, input, (int) strlen(input), 0);
        }
    }
    return 0;
}
// 清理资源，确保程序退出时释放所有资源
void Cleanup() {
    // 关闭所有客户端连接
    std::lock_guard<std::mutex> lock(connectionMutex);
    for (auto client: clients) {
        closesocket(client->sclient);
        delete client;
    }
    clients.clear();
    userMap.clear();  // 清理用户映射

    WSACleanup();// 关闭服务器套接字
}
