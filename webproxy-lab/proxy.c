#include <stdio.h>
#include <errno.h>
#include <string.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";


void signal_handler(int aig)
{
  while (1){
    if (waitpid(-1, 0, WNOHANG) <= 0)
      break;
  }
}

void parse_url(const char *url, char *hostname, char *hostport, char* uri) {
  const char *p = url;

  /* 1) "http://" 있으면 건너뜀 */
  if (strncmp(p, "http://", 7) == 0)
      p += 7;

  /* 2) 호스트:포트 구간의 끝(슬래시) 찾기 */
  const char *slash = strchr(p, '/');
  const char *hostend = slash ? slash : p + strlen(p);

  /* 3) 포트 구분자인 ':' 위치(호스트 구간 내에서만) */
  const char *colon = memchr(p, ':', (size_t)(hostend - p));

  if (colon) {
      /* host */
      size_t hlen = (size_t)(colon - p);
      memcpy(hostname, p, hlen);
      hostname[hlen] = '\0';

      /* port */
      size_t plen = (size_t)(hostend - colon - 1);
      memcpy(hostport, colon + 1, plen);
      hostport[plen] = '\0';
  } else {
      /* 포트 미지정 → 80 */
      size_t hlen = (size_t)(hostend - p);
      memcpy(hostname, p, hlen);
      hostname[hlen] = '\0';
      strcpy(hostport, "80");
  }
  strcpy(uri, slash ? slash : p + strlen(p));
}

void handle_response(int clientfd, int serverfd) {
  char buf[MAXLINE];
  rio_t rio;
  size_t n;

  Rio_readinitb(&rio, serverfd);
  while((n = Rio_readnb(&rio, buf, MAXLINE)) > 0)
    Rio_writen(clientfd, buf, n);
}

void handle_request(int clientfd) {
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE], hostname[MAXLINE], hostport[MAXLINE], uri[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, clientfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  sscanf(buf, "%s %s %s", method, url, version);
  printf("method: %s, uri: %s, version: %s\n", method, uri, version);
  parse_url(url, hostname, hostport, uri);

  printf("hostname: %s, hostport: %s, uri: %s\n", hostname, hostport, uri);

  int serverfd = Open_clientfd(hostname, hostport);

  strcpy(buf, "GET ");
  strcat(buf, uri);
  strcat(buf, " HTTP/1.1\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  while(Rio_readlineb(&rio, buf, MAXLINE) > 0) {
    printf("%s", buf);
    if (strstr(buf, "User-Agent: ")) {
      strcpy(buf, user_agent_hdr);
    }
    else if (strstr(buf, "Proxy-Connection: ")) {
      strcpy(buf, "Proxy-Connection: close\r\n");
    }
    else if (strstr(buf, "Connections: ")) {
      strcpy(buf, "Connections: close\r\n");
    }
    Rio_writen(serverfd, buf, strlen(buf));
    if (strcmp(buf, "\r\n") == 0)
      break;
  }

  handle_response(clientfd, serverfd);

  Close(serverfd);
}

int main(int argc, char **argv)
{
  int listenfd, clientfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  listenfd = Open_listenfd(argv[1]);
  clientlen = sizeof(clientaddr);

  signal(SIGCHLD, signal_handler);
  
  while (1) {
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (Fork() == 0) {
      Close(listenfd);
      handle_request(clientfd);
      Close(clientfd);
      exit(0);
    }
    Close(clientfd);
  }
  Close(listenfd);
  exit(0);
}
