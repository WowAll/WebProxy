#include "csapp.h"

int main(int argc, char **argv)
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    /* 인자 체크 */
    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }

    /* 인자 할당 (호스트 이름, 포트 번호) */
    host = argv[1];
    port = argv[2];

    /* 소켓 열기 */
    clientfd = Open_clientfd(host, port);
    /* 버퍼 초기화 */
    Rio_readinitb(&rio, clientfd);

    /* 입력 받은 메시지를 서버에 전송 */
    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        Rio_writen(clientfd, buf, strlen(buf));

        /* 서버에서 받은 메시지를 출력 */
        Rio_readlineb(&rio, buf, MAXLINE);
        Fputs(buf, stdout);
    }
    /* 소켓 닫기 */
    Close(clientfd);
    /* 프로그램 종료 */
    exit(0);
}