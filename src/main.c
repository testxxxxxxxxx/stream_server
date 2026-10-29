#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "../include/main.h"

#define MAX_BUFFER 4096

typedef struct {
    int port, maxEvents;
    const char* filesDir;
} ServerConfig;

void setnotblock(int fd) {
   int flags = fcntl(fd, F_GETFL);
   fcntl(fd, F_SETFL, flags | O_NONBLOCK); 
}

void sendToClient(int cfd, ServerConfig sc) {
    char buffer[MAX_BUFFER] = {0};
    int rc = read(cfd, buffer, MAX_BUFFER);

    char fileName[256] = {0}, fullPath[512] = {0};
    int status = sscanf(buffer, "GET /%255s ", fileName);
    if(status != 1) {
        char* badRequest = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        write(cfd, badRequest, strlen(badRequest));
        close(cfd);
        return;
    }
    if(strlen(fileName) == 0 || strcmp(fileName, "HTTP/1.1") == 0)
        strcpy(fileName, "index.html");
    snprintf(fullPath, sizeof(fullPath), "%s%s", sc.filesDir, fileName); 
    int fd = open(fullPath, O_RDONLY);
    if(fd < 0) {
        char* notFound = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
        write(cfd, notFound, strlen(notFound));
        close(cfd);
        return;
    }
    struct stat stat_buf;
    fstat(fd, &stat_buf);
    const char* contentType = "text/plain";
    if (strstr(fileName, ".m3u8")) {
        contentType = "application/vnd.apple.mpegurl";
    } else if (strstr(fileName, ".ts")) {
        contentType = "video/mp2t";
    } else if (strstr(fileName, ".html")) {
        contentType = "text/html; charset=UTF-8";
    } else if (strstr(fileName, ".js")) {
        contentType = "application/javascript";
    }
    char header[512] = {0};
    sprintf(header, "HTTP/1.1 200 OK\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Access-Control-Expose-Headers: Content-Length, Content-Type\r\n"
                    "Content-Type: %s\r\n"
                    "Content-Length: %ld\r\n"
                    "Accept-Ranges: none\r\n"
                    "Connection: close\r\n\r\n", contentType, stat_buf.st_size);
    write(cfd, header, strlen(header));
    off_t offset = 0;
    sendfile(cfd, fd, &offset, stat_buf.st_size);
    close(fd);
    close(cfd);
}

int main(int argc, char** argv) {
    if(argc < 2) {
        perror("Usage: ./server <PORT>");
        return -1;
    }
    ServerConfig sc = {.port = atoi(argv[1]), .maxEvents = 1000, .filesDir = "files/"};
    int sfd = socket(AF_INET, SOCK_STREAM, 0), opt = 1;
    if(sfd < 0) {
        perror("Socket: ");
        return sfd;
    }
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in saddr = {.sin_family = AF_INET, .sin_addr.s_addr = INADDR_ANY, .sin_port = htons(sc.port)};
    int status = bind(sfd, (struct sockaddr*)& saddr, sizeof(saddr));
    if(status < 0) {
        perror("Bind: ");
        return status;
    }
    status = listen(sfd, 10);
    if(status < 0) {
        perror("Listen: ");
        return status;
    }
    setnotblock(sfd);
    int efd = epoll_create1(0);
    if(efd < 0) {
        perror("Epoll create");
        return efd;
    }

    struct epoll_event event, events[sc.maxEvents];
    event.events = EPOLLIN;
    event.data.fd = sfd;
    epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
    while(1) {
        int countReady = epoll_wait(efd, events, sc.maxEvents, -1);

        for(int i = 0; i < countReady; i++) {
            struct sockaddr caddr;
            socklen_t caddrSize = sizeof(caddr);
            int pollFd = events[i].data.fd;
            if(pollFd == sfd) {
                int cfd = accept(sfd, (struct sockaddr*)& caddr, &caddrSize);
                setnotblock(cfd);
                event.events = EPOLLIN;
                event.data.fd = cfd;
                epoll_ctl(efd, EPOLL_CTL_ADD, cfd, &event);
            }
            else {
                if(events[i].events & EPOLLIN) {
                    sendToClient(pollFd, sc);
                    epoll_ctl(efd, EPOLL_CTL_DEL, pollFd, NULL);
                }
                if(events[i].events & EPOLLOUT) {

                }
            }
        }
    }
    close(sfd);
    shutdown(sfd, SHUT_RDWR);
    return 0;
}
