#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>
#include <Winsock2.h>

#pragma comment(lib, "WS2_32.lib")

// 定义接收缓冲区大小
constexpr size_t BUF_SIZE = 4096;
// 使用原子类型控制接收循环的继续或退出
std::atomic<bool> continueReceiving{true};

void SendToServer(SOCKET sclient, const char *data, int len, int i) {
    WSABUF buffer;
    buffer.buf = (CHAR*)data;
    buffer.len = len;
    DWORD bytesSent;
    OVERLAPPED ol;
    ZeroMemory(&ol, sizeof(ol));
    int result = WSASend(sclient, &buffer, 1, &bytesSent, 0, &ol, NULL);
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        std::cerr << "WSASend failed with error: " << WSAGetLastError() << std::endl;
    }
}

/**
 * 接收消息线程函数
 * @param sHost 服务器套接字
 */
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
            // 非阻塞模式下接收失败时休眠一段时间后重试
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

/**
 * 初始化套接字并连接服务器
 * @param sHost 服务器套接字引用
 * @param ip 服务器IP地址
 * @param port 服务器端口号
 * @return 初始化是否成功
 */
int initializeSocket(SOCKET &sHost, const std::string &ip, const std::string &port) {
    WSADATA wsd;
    // 初始化Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsd) != 0) {
        std::cerr << "WSAStartup failed!" << std::endl;
        return -1;
    }

    // 创建TCP套接字
    sHost = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sHost == INVALID_SOCKET) {
        std::cerr << "socket creation failed!" << std::endl;
        WSACleanup();
        return -1;
    }

    SOCKADDR_IN servAddr;
    // 设置服务器地址信息
    servAddr.sin_family = AF_INET;
    servAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    servAddr.sin_port = htons(static_cast<u_short>(std::stoi(port)));

    // 连接服务器
    if (connect(sHost, reinterpret_cast<LPSOCKADDR>(&servAddr), sizeof(servAddr)) == SOCKET_ERROR) {
        std::cerr << "Connect failed!" << std::endl;
        closesocket(sHost);
        WSACleanup();
        return -1;
    }

    std::cout << "Connected to server successfully." << std::endl;
    return 0;
}

/**
 * 清理套接字和Winsock
 * @param sHost 服务器套接字
 */
void cleanup(SOCKET sHost) {
    closesocket(sHost);
    WSACleanup();
}

int main(int argc, char **argv) {
    // 检查命令行参数数量
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <Server IP> <Port>" << std::endl;
        return 1;
    }

    SOCKET sHost;
    // 初始化套接字并连接服务器
    if (initializeSocket(sHost, argv[1], argv[2]) != 0) {
        return 1;
    }

    // 获取并发送注册信息
    std::string username;
    std::cout << "Enter your username: ";
    std::cin >> username;
    std::string registerMessage = "REGISTER SERVER " + username;
    SendToServer(sHost, registerMessage.c_str(), (int) registerMessage.size(), 0);

    // 启动接收消息的线程
    std::thread receiverThread(receiveMessages, sHost);

    std::string input;

    //清空输入缓冲区
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

    // 主循环，接收和处理用户输入
    while (true) {
        std::getline(std::cin, input);
        if (input == "exit") {
            continueReceiving = false;  // 告诉接收线程停止接收消息
            std::string removeMessage = "REMOVE SERVER " + username;
            SendToServer(sHost, removeMessage.c_str(), (int) removeMessage.size(), 0);  // 发送移除用户的消息
            break;
        } else if (input.substr(0, 6) == "CREATE") {
            std::string groupName = input.substr(input.find(' ') + 1);
            std::string createGroupMessage = "CREATE_GROUP " + groupName;
            SendToServer(sHost, createGroupMessage.c_str(), (int) createGroupMessage.size(), 0);
        } else if (input.substr(0, 5) == "GROUP") {
            std::string groupName = input.substr(0, input.find(' '));
            std::string message = input.substr(input.find(' '), input.size());
            std::string groupMessage = "GROUP_MESSAGE ";
            groupMessage.append(groupName).append(" ").append(message);
            SendToServer(sHost, groupMessage.c_str(), (int) groupMessage.size(), 0);
        } else if (input.substr(0, 5) == "CHECK") {
            std::string groupName = input.substr(input.find(' ') + 1);
            std::string groupMessage = "GROUP_CHECK " + groupName;
            SendToServer(sHost, groupMessage.c_str(), (int) groupMessage.size(), 0);
        } else if (input.substr(0, 4) == "JOIN") {
            const std::string &groupName = input;
            std::string joinGroupMessage = "JOIN_GROUP " + groupName;
            SendToServer(sHost, joinGroupMessage.c_str(), (int) joinGroupMessage.size(), 0);
        } else if (input.empty()) {
            std::cout << "Invalid input!" << std::endl;
        } else {
            std::string message = "MESSAGE " + input;
            SendToServer(sHost, message.c_str(), (int) message.size(), 0);
        }
    }

    // 等待接收消息线程结束
    receiverThread.join();
    // 清理套接字和Winsock
    cleanup(sHost);
    return 0;
}
