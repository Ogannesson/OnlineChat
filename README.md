# 一款用于Windows的网络聊天程序。
* 客户端之间的通信通过服务器进行转发，群聊采用多播或组播。
* 服务器和客户端之间使用WinSock API 通信。
* 服务器可以发送广播消息，且实时向所有客户端发送用户在线信息。
* 使用网页作为用户界面，服务器和客户端都使用http API 与前端网页通信。
* 操作说明：
* 1. 创建群组
create [groupname]
* 2. 查看群组成员
check [groupname]
* 3. 加入群组
join [groupname]
* 4. 发送群组消息
group [groupname] [message]
* 5. 发送私聊消息
[username] [message]
* 6. 退出聊天
exit