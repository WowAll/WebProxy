#include "csapp.h"

void echo(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    /* 버퍼 초기화 */
    Rio_readinitb(&rio, connfd);

    /* 메시지 수신 */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        /* 메시지 출력 */
        printf("server received %d bytes\n", (int)n);
        printf("received: %s", buf);

        /* 메시지 전송 */
        Rio_writen(connfd, buf, n);
    }
}

int main(int argc, char **argv)
{
    /* 연결용 소켓, 클라이언트 소켓 */
    int listenfd, connfd[1000];
    /* 클라이언트 정보 */
    socklen_t clientlen;
    /* 클라이언트 주소 */
    struct sockaddr_storage clientaddr;
    /* 클라이언트 호스트 이름, 포트 번호 */
    char client_hostname[MAXLINE], client_port[MAXLINE];

    /* 인자 체크 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    /* 리스닝 소켓 열기 */
    listenfd = Open_listenfd(argv[1]);
    /* 연결 수락 대기 */
    while (1) {
        /* 클라이언트 정보 수신 */
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        /* 클라이언트 정보 출력 */
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        /* 메시지 처리 */
        echo(connfd);
        /* 연결 종료 */
        printf("Closed connection to (%s, %s)\n", client_hostname, client_port);
        Close(connfd);
    }
    /* 소켓 닫기 */
    Close(listenfd);
    
    /* 프로그램 종료 */
    exit(0);
}