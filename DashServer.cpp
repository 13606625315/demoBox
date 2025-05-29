#include "DashServer.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif



// 定义HTTP响应头
const char* HTTP_200_OK = "HTTP/1.1 200 OK\r\n";
const char* HTTP_404_NOT_FOUND = "HTTP/1.1 404 Not Found\r\n";
const char* HTTP_500_ERROR = "HTTP/1.1 500 Internal Server Error\r\n";
const char* CONTENT_TYPE_MPD = "Content-Type: application/dash+xml\r\n";
const char* CONTENT_TYPE_MP4 = "Content-Type: video/mp4\r\n";
const char* CONTENT_TYPE_HTML = "Content-Type: text/html\r\n";
const char* CORS_HEADER = "Access-Control-Allow-Origin: *\r\n";

DashServer::DashServer() : m_running(false), m_serverSocket(-1), m_port(8080), m_segmentDuration(4.0f) {
    // 初始化GPAC
    gf_sys_init(GF_MemTrackerNone);
}

DashServer::~DashServer() {
    stop();
    // 清理GPAC
    gf_sys_close();
}

    // 初始化服务器
    bool DashServer::init(uint16_t port, float segmentDuration, const std::string& outputDir) {
        m_port = port;
        m_segmentDuration = segmentDuration;
        m_outputDir = outputDir;

        // 确保输出目录存在
        struct stat st;
        if (stat(outputDir.c_str(), &st) != 0) {
            // 目录不存在，创建目录
#ifdef _WIN32
            // Windows平台
            if (system(("mkdir \"" + outputDir + "\"").c_str()) != 0) {
                std::cerr << "创建输出目录失败: " << outputDir << std::endl;
                return false;
            }
#else
            // Linux/Unix平台
            if (system(("mkdir -p \"" + outputDir + "\"").c_str()) != 0) {
                std::cerr << "创建输出目录失败: " << outputDir << std::endl;
                return false;
            }
#endif
        }

        return true;
    }

    // 添加MP4文件
    bool DashServer::addMP4File(const std::string& mp4FilePath, const std::string& streamName) {
        // 检查文件是否存在
        struct stat st;
        if (stat(mp4FilePath.c_str(), &st) != 0) {
            std::cerr << "MP4文件不存在: " << mp4FilePath << std::endl;
            return false;
        }

        // 创建流输出目录
        std::string streamDir = m_outputDir + "/" + streamName;
#ifdef _WIN32
        system(("mkdir \"" + streamDir + "\" 2>nul").c_str());
#else
        system(("mkdir -p \"" + streamDir + "\"").c_str());
#endif

        // 使用GPAC的MP4Box工具进行DASH分段
        std::string cmd = "MP4Box -dash " + std::to_string((int)(m_segmentDuration * 1000)) +
                         " -frag " + std::to_string((int)(m_segmentDuration * 1000)) +
                         " -rap -segment-name segment_ -out \"" + streamDir + "/manifest.mpd\" \"" +
                         mp4FilePath + "\"";

        std::cout << "执行命令: " << cmd << std::endl;
        int result = system(cmd.c_str());
        if (result != 0) {
            std::cerr << "分段MP4文件失败: " << mp4FilePath << std::endl;
            return false;
        }

        // 添加到流列表
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        m_streams[streamName] = mp4FilePath;

        return true;
    }

    // 启动服务器
    bool DashServer::start() {
        if (m_running) {
            std::cerr << "服务器已经在运行" << std::endl;
            return false;
        }

#ifdef _WIN32
        // 初始化Winsock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
            std::cerr << "WSAStartup失败" << std::endl;
            return false;
        }
#endif

        // 创建套接字
        m_serverSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (m_serverSocket < 0) {
            std::cerr << "创建套接字失败" << std::endl;
#ifdef _WIN32
            WSACleanup();
#endif
            return false;
        }

        // 设置套接字选项
        int opt = 1;
#ifdef _WIN32
        if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
#else
        if (setsockopt(m_serverSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
#endif
            std::cerr << "设置套接字选项失败" << std::endl;
#ifdef _WIN32
            closesocket(m_serverSocket);
            WSACleanup();
#else
            close(m_serverSocket);
#endif
            return false;
        }

        // 绑定地址
        struct sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(m_port);

        if (bind(m_serverSocket, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "绑定套接字失败" << std::endl;
#ifdef _WIN32
            closesocket(m_serverSocket);
            WSACleanup();
#else
            close(m_serverSocket);
#endif
            return false;
        }

        // 监听连接
        if (listen(m_serverSocket, 10) < 0) {
            std::cerr << "监听套接字失败" << std::endl;
#ifdef _WIN32
            closesocket(m_serverSocket);
            WSACleanup();
#else
            close(m_serverSocket);
#endif
            return false;
        }

        // 启动服务器线程
        m_running = true;
        m_serverThread = std::thread(&DashServer::serverLoop, this);

        std::cout << "DASH服务器已启动，监听端口: " << m_port << std::endl;
        std::cout << "访问地址: http://localhost:" << m_port << "/" << std::endl;

        return true;
    }

    // 停止服务器
    void DashServer::stop() {
        if (!m_running) {
            return;
        }

        m_running = false;

        // 关闭服务器套接字
#ifdef _WIN32
        closesocket(m_serverSocket);
        WSACleanup();
#else
        close(m_serverSocket);
#endif

        // 等待服务器线程结束
        if (m_serverThread.joinable()) {
            m_serverThread.join();
        }

        std::cout << "DASH服务器已停止" << std::endl;
    }


    // 服务器主循环
    void DashServer::serverLoop() {
        while (m_running) {
            // 接受连接
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);

#ifdef _WIN32
            SOCKET clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket == INVALID_SOCKET) {
                if (m_running) {
                    std::cerr << "接受连接失败: " << WSAGetLastError() << std::endl;
                }
                continue;
            }
#else
            int clientSocket = accept(m_serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
            if (clientSocket < 0) {
                if (m_running) {
                    std::cerr << "接受连接失败" << std::endl;
                }
                continue;
            }
#endif

            // 创建客户端处理线程
            std::thread clientThread(&DashServer::handleClient, this, clientSocket);
            clientThread.detach();
        }
    }

    // 处理客户端请求
    void DashServer::handleClient(int clientSocket) {
        // 接收HTTP请求
        char buffer[4096] = {0};
        int bytesRead;

#ifdef _WIN32
        bytesRead = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
#else
        bytesRead = read(clientSocket, buffer, sizeof(buffer) - 1);
#endif

        if (bytesRead <= 0) {
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            return;
        }

        // 解析HTTP请求
        std::string request(buffer);
        std::string method, path, version;
        std::istringstream requestStream(request);
        requestStream >> method >> path >> version;

        // 只处理GET请求
        if (method != "GET") {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1></body></html>");
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            return;
        }

        // 处理根路径请求
        if (path == "/") {
            handleRootRequest(clientSocket);
        }
        // 处理MPD请求
        else if (path.find(".mpd") != std::string::npos) {
            handleMPDRequest(clientSocket, path);
        }
        // 处理分段请求
        else if (path.find(".m4s") != std::string::npos || path.find(".mp4") != std::string::npos) {
            handleSegmentRequest(clientSocket, path);
        }
        // 处理未知请求
        else {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1></body></html>");
        }

#ifdef _WIN32
        closesocket(clientSocket);
#else
        close(clientSocket);
#endif
    }

    // 处理根路径请求
    void DashServer::handleRootRequest(int clientSocket) {
        // 生成可用流列表
        std::ostringstream html;
        html << "<!DOCTYPE html>\n"
             << "<html>\n"
             << "<head>\n"
             << "<title>DASH Streaming Server</title>\n"
             << "<style>\n"
             << "body { font-family: Arial, sans-serif; margin: 40px; }\n"
             << "h1 { color: #333; }\n"
             << "ul { list-style-type: none; padding: 0; }\n"
             << "li { margin: 10px 0; }\n"
             << "a { color: #0066cc; text-decoration: none; }\n"
             << "a:hover { text-decoration: underline; }\n"
             << "</style>\n"
             << "</head>\n"
             << "<body>\n"
             << "<h1>DASH流媒体服务器</h1>\n"
             << "<h2>可用的流:</h2>\n"
             << "<ul>\n";

        std::lock_guard<std::mutex> lock(m_streamsMutex);
        for (const auto& stream : m_streams) {
            html << "<li><a href='/" << stream.first << "/manifest.mpd'>" << stream.first << "</a></li>\n";
        }

        html << "</ul>\n"
             << "<div style='margin-top: 30px;'>\n"
             << "<h3>播放说明:</h3>\n"
             << "<p>1. 点击上面的链接获取MPD文件</p>\n"
             << "<p>2. 使用支持DASH的播放器播放，如VLC、dash.js等</p>\n"
             << "</div>\n"
             << "<div style='margin-top: 20px; font-size: 12px; color: #666;'>\n"
             << "基于GPAC库实现的DASH流媒体服务器\n"
             << "</div>\n"
             << "</body>\n"
             << "</html>\n";

        sendResponse(clientSocket, HTTP_200_OK, CONTENT_TYPE_HTML, html.str());
    }

    // 处理MPD请求
    void DashServer::handleMPDRequest(int clientSocket, const std::string& path) {
        // 解析路径，获取流名称
        std::string streamName;
        size_t pos = path.find('/');
        if (pos != std::string::npos) {
            streamName = path.substr(1, path.find('/', pos + 1) - 1);
        }

        // 检查流是否存在
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        if (m_streams.find(streamName) == m_streams.end()) {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1><p>Stream not found</p></body></html>");
            return;
        }

        // MPD文件路径
        std::string mpdPath = m_outputDir + "/" + streamName + "/manifest.mpd";

        // 读取MPD文件
        std::ifstream file(mpdPath, std::ios::binary);
        if (!file) {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1><p>MPD file not found</p></body></html>");
            return;
        }

        // 读取文件内容
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();

        // 发送MPD文件
        sendResponse(clientSocket, HTTP_200_OK, CONTENT_TYPE_MPD, content);
    }

    // 处理分段请求
    void DashServer::handleSegmentRequest(int clientSocket, const std::string& path) {
        // 解析路径，获取流名称和文件名
        std::string streamName;
        std::string fileName;

        size_t pos = path.find('/');
        if (pos != std::string::npos) {
            streamName = path.substr(1, path.find('/', pos + 1) - 1);
            fileName = path.substr(path.find('/', pos + 1) + 1);
        }

        // 检查流是否存在
        std::lock_guard<std::mutex> lock(m_streamsMutex);
        if (m_streams.find(streamName) == m_streams.end()) {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1><p>Stream not found</p></body></html>");
            return;
        }

        // 分段文件路径
        std::string segmentPath = m_outputDir + "/" + streamName + "/" + fileName;

        // 读取分段文件
        std::ifstream file(segmentPath, std::ios::binary);
        if (!file) {
            sendResponse(clientSocket, HTTP_404_NOT_FOUND, CONTENT_TYPE_HTML, "<html><body><h1>404 Not Found</h1><p>Segment file not found</p></body></html>");
            return;
        }

        // 获取文件大小
        file.seekg(0, std::ios::end);
        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        // 读取文件内容
        std::vector<char> buffer(fileSize);
        file.read(buffer.data(), fileSize);

        // 发送分段文件
        sendResponse(clientSocket, HTTP_200_OK, CONTENT_TYPE_MP4, std::string(buffer.data(), fileSize));
    }

    // 发送HTTP响应
    void DashServer::sendResponse(int clientSocket, const char* status, const char* contentType, const std::string& content) {
        std::ostringstream response;
        response << status;
        response << contentType;
        response << CORS_HEADER;
        response << "Content-Length: " << content.size() << "\r\n";
        response << "\r\n";
        response << content;

        std::string responseStr = response.str();
#ifdef _WIN32
        send(clientSocket, responseStr.c_str(), responseStr.size(), 0);
#else
        write(clientSocket, responseStr.c_str(), responseStr.size());
#endif
    }



// // 主函数
// int main(int argc, char* argv[]) {
//     if (argc < 2) {
//         std::cout << "用法: " << argv[0] << " <MP4文件路径> [端口号] [流名称]" << std::endl;
//         return 1;
//     }

//     std::string mp4FilePath = argv[1];
//     uint16_t port = (argc > 2) ? std::stoi(argv[2]) : 8080;
//     std::string streamName = (argc > 3) ? argv[3] : "video";

//     // 创建DASH服务器
//     DashServer server;

//     // 初始化服务器
//     if (!server.init(port, 4.0f, "./dash")) {
//         std::cerr << "初始化DASH服务器失败" << std::endl;
//         return 1;
//     }

//     // 添加MP4文件
//     if (!server.addMP4File(mp4FilePath, streamName)) {
//         std::cerr << "添加MP4文件失败: " << mp4FilePath << std::endl;
//         return 1;
//     }

//     // 启动服务器
//     if (!server.start()) {
//         std::cerr << "启动DASH服务器失败" << std::endl;
//         return 1;
//     }

//     std::cout << "DASH服务器已启动，按Enter键停止服务器..." << std::endl;
//     std::cout << "访问地址: http://localhost:" << port << "/" << streamName << "/manifest.mpd" << std::endl;

//     // 等待用户输入
//     std::cin.get();

//     // 停止服务器
//     server.stop();

//     return 0;
// }