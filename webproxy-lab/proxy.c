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

typedef struct cache_entry {
    char *key;                 // URL
    char *obj;                 // 응답(헤더+바디)
    size_t size;               // obj 길이
    struct cache_entry *prev, *next; // LRU 리스트 포인터
} cache_entry_t;
  
typedef struct {
    cache_entry_t *head;       // MRU(가장 최근)
    cache_entry_t *tail;       // LRU(가장 오래된)
    size_t total;              // 현재 총 바이트
    pthread_rwlock_t rwlock;   // 전역 RW 락
} cache_t;
  
static cache_t g_cache;

static void make_cache_key(char *key, size_t ksz, const char *host, const char *port, const char *uri) {
    snprintf(key, ksz, "%s:%s%s", host, port, uri);
}

static void move_to_front(cache_t *c, cache_entry_t *e) {
  if (c->head == e) return;
  // detach
  if (e->prev) e->prev->next = e->next;
  if (e->next) e->next->prev = e->prev;
  if (c->tail == e) c->tail = e->prev;
  // insert front
  e->prev = NULL;
  e->next = c->head;
  if (c->head) c->head->prev = e;
  c->head = e;
  if (!c->tail) c->tail = e;
}

static void evict_until(cache_t *c, size_t need) {
  while (c->total + need > MAX_CACHE_SIZE && c->tail) {
      cache_entry_t *victim = c->tail;
      c->total -= victim->size;
      c->tail = victim->prev;
      if (c->tail) c->tail->next = NULL;
      if (c->head == victim) c->head = NULL;
      Free(victim->obj);
      Free(victim->key);
      Free(victim);
  }
}

void cache_init(cache_t *c) {
  c->head = c->tail = NULL;
  c->total = 0;
  pthread_rwlock_init(&c->rwlock, NULL);
}

void cache_deinit(cache_t *c) {
  pthread_rwlock_wrlock(&c->rwlock);
  for (cache_entry_t *e=c->head; e;) {
      cache_entry_t *n = e->next;
      Free(e->obj);
      Free(e->key);
      Free(e);
      e = n;
  }
  c->head = c->tail = NULL;
  c->total = 0;
  pthread_rwlock_unlock(&c->rwlock);
  pthread_rwlock_destroy(&c->rwlock);
}

cache_entry_t* cache_lookup(cache_t *c, const char *key) {
  pthread_rwlock_wrlock(&c->rwlock); // 조회 + LRU 갱신 필요 → WR 락 잡음
  for (cache_entry_t *e=c->head; e; e=e->next) {
      if (strcmp(e->key, key) == 0) {
          move_to_front(c, e);
          pthread_rwlock_unlock(&c->rwlock);
          return e; // 주의: 포인터는 캐시 내부 메모리
      }
  }
  pthread_rwlock_unlock(&c->rwlock);
  return NULL;
}

void cache_insert(cache_t *c, const char *key, const char *obj, size_t size) {
  if (size > MAX_OBJECT_SIZE) return; // 정책: 너무 크면 저장 안 함
  pthread_rwlock_wrlock(&c->rwlock);
  // 중복 키가 이미 있으면 갱신 대신 LRU만 최신화하고 종료(단순 정책)
  for (cache_entry_t *e=c->head; e; e=e->next) {
      if (strcmp(e->key, key) == 0) {
          move_to_front(c, e);
          pthread_rwlock_unlock(&c->rwlock);
          return;
      }
  }
  evict_until(c, size);
  cache_entry_t *e = Malloc(sizeof(*e));
  e->key = Malloc(strlen(key)+1);
  strcpy(e->key, key);
  e->obj = Malloc(size);
  memcpy(e->obj, obj, size);
  e->size = size;
  e->prev = e->next = NULL;
  // push front
  e->next = c->head;
  if (c->head) c->head->prev = e;
  c->head = e;
  if (!c->tail) c->tail = e;
  c->total += size;
  pthread_rwlock_unlock(&c->rwlock);
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

void handle_response(int clientfd, int serverfd, const char *key) {
  char buf[MAXLINE];
  rio_t rio;
  size_t n;
  char *obj = Malloc(MAX_OBJECT_SIZE);
  size_t obj_size = 0;
  int flag = 1;

  Rio_readinitb(&rio, serverfd);
  while((n = Rio_readnb(&rio, buf, MAXLINE)) > 0) {
    Rio_writen(clientfd, buf, n);
    if (n + obj_size <= MAX_OBJECT_SIZE) {
      memcpy(obj + obj_size, buf, n);
      obj_size += n;
    }
    else
      flag = 0;
  }
  if (flag)
    cache_insert(&g_cache, key, obj, obj_size);
  Free(obj);
  Close(serverfd);
}

void* handle_request(void* argp) {
  int clientfd = *((int *)argp);
  Free(argp);
  Pthread_detach(Pthread_self());

  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE], hostname[MAXLINE], hostport[MAXLINE], uri[MAXLINE], key[3 * MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, clientfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  sscanf(buf, "%s %s %s", method, url, version);
  printf("method: %s, uri: %s, version: %s\n", method, uri, version);
  parse_url(url, hostname, hostport, uri);
  make_cache_key(key, sizeof(key), hostname, hostport, uri);

  printf("hostname: %s, hostport: %s, uri: %s\n", hostname, hostport, uri);

  cache_entry_t *e = cache_lookup(&g_cache, key);
  if (e) {
    Rio_writen(clientfd, e->obj, e->size);
    close(clientfd);
    return NULL;
  }

  int serverfd = Open_clientfd(hostname, hostport);

  if (serverfd < 0) {
    Close(clientfd);
    return NULL;
  }

  strcpy(buf, "GET ");
  strcat(buf, uri);
  strcat(buf, " HTTP/1.1\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  while(Rio_readlineb(&rio, buf, MAXLINE) > 0) {
    printf("%s", buf);
    if (strstr(buf, "User-Agent: ")) {
      strcpy(buf, user_agent_hdr);
    }
    else if (strstr(buf, "Proxy-Connection: "))
      continue;
    else if (strstr(buf, "Host: "))
      continue;
    else if (strstr(buf, "Connections: "))
      continue;
    Rio_writen(serverfd, buf, strlen(buf));
    if (strcmp(buf, "\r\n") == 0)
      break;
  }
  handle_response(clientfd, serverfd, key);
  Close(clientfd);
  return NULL;
}

int main(int argc, char **argv)
{
  int listenfd, *clientfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  listenfd = Open_listenfd(argv[1]);
  clientlen = sizeof(clientaddr);
  cache_init(&g_cache);
  while (1) {
    clientfdp = (int *)Malloc(sizeof(int));
    *clientfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Pthread_create(&tid, NULL, handle_request, clientfdp);
  }
  cache_deinit(&g_cache);
  Close(listenfd);
  exit(0);
}
