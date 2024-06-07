#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>


int initializeWinSock() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup failed: " << result << std::endl;
        return result;
    }
    std::cout << "WinSock initialized successfully." << std::endl;
    return 0;
}

SOCKET connectToServer(const char* serverIP, const char* serverPort) {
    struct addrinfo hints{}, *result = nullptr;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(serverIP, serverPort, &hints, &result);
    if (status != 0) {
        std::cerr << "getaddrinfo failed: " << status << std::endl;
        WSACleanup();
        return INVALID_SOCKET;
    }

    SOCKET connectSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (connectSocket == INVALID_SOCKET) {
        std::cerr << "Error at socket(): " << WSAGetLastError() << std::endl;
        freeaddrinfo(result);
        WSACleanup();
        return INVALID_SOCKET;
    }

    if (connect(connectSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR) {
        std::cerr << "Unable to connect to server: " << WSAGetLastError() << std::endl;
        closesocket(connectSocket);
        freeaddrinfo(result);
        WSACleanup();
        return INVALID_SOCKET;
    }

    std::cout << "Successfully connected to server at " << serverIP << ":" << serverPort << std::endl;
    freeaddrinfo(result);
    return connectSocket;
}

void sendAndReceiveData(SOCKET connectSocket) {
    const char* sendMessage = "Hello, Server!";
    int bytesSent = send(connectSocket, sendMessage, (int)strlen(sendMessage), 0);
    if (bytesSent == SOCKET_ERROR) {
        std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
        closesocket(connectSocket);
        WSACleanup();
        return;
    }

    std::cout << "Message sent to server: " << sendMessage << std::endl;

    char recvBuffer[512];
    int bytesReceived = recv(connectSocket, recvBuffer, 512, 0);
    if (bytesReceived > 0) {
        recvBuffer[bytesReceived] = '\0'; // Ensure null termination
        std::cout << "Received message from server: " << recvBuffer << std::endl;
    } else if (bytesReceived == 0) {
        std::cout << "Connection closed by server.\n";
    } else {
        std::cerr << "recv failed: " << WSAGetLastError() << std::endl;
    }
}

int main() {
    int result = initializeWinSock();
    if (result != 0) {
        return 1;
    }

    SOCKET connectSocket = connectToServer("127.0.0.1", "12345");  // 使用服务器的IP地址和端口号
    if (connectSocket == INVALID_SOCKET) {
        return 1;
    }

    sendAndReceiveData(connectSocket);

    // 清理资源
    closesocket(connectSocket);
    WSACleanup();
    return 0;
}
