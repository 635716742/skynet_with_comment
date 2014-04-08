#ifndef skynet_socket_server_h
#define skynet_socket_server_h

#include <stdint.h>

// socket_server_poll���ص�socket��Ϣ����
#define SOCKET_DATA 0    // data ����
#define SOCKET_CLOSE 1   // close conn
#define SOCKET_OPEN 2    // conn ok
#define SOCKET_ACCEPT 3  // �������ӽ��� (Accept���������ӵ�fd ����δ����epoll������)
#define SOCKET_ERROR 4   // error
#define SOCKET_EXIT 5    // exit

struct socket_server;

// socket_server��Ӧ��msg
struct socket_message {
	int id; // Ӧ�ò��socket fd
	uintptr_t opaque; // ��skynet�ж�Ӧһ��actorʵ���handler
	int ud;	//����accept������˵�������ӵ�fd �������ݵ��������ݵĴ�С
	char * data;
};

struct socket_server * socket_server_create();
void socket_server_release(struct socket_server *);
int socket_server_poll(struct socket_server *, struct socket_message *result, int *more);

void socket_server_exit(struct socket_server *);
void socket_server_close(struct socket_server *, uintptr_t opaque, int id);
void socket_server_start(struct socket_server *, uintptr_t opaque, int id);

// return -1 when error
int64_t socket_server_send(struct socket_server *, int id, const void * buffer, int sz);

// ctrl command below returns id
int socket_server_listen(struct socket_server *, uintptr_t opaque, const char * addr, int port, int backlog);
int socket_server_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);
int socket_server_bind(struct socket_server *, uintptr_t opaque, int fd);

int socket_server_block_connect(struct socket_server *, uintptr_t opaque, const char * addr, int port);

#endif
