#ifndef SKYNET_HARBOR_H
#define SKYNET_HARBOR_H

#include <stdint.h>
#include <stdlib.h>

#define GLOBALNAME_LENGTH 16 // ȫ�����ֵĳ���
#define REMOTE_MAX 256

// reserve high 8 bits for remote id
#define HANDLE_MASK 0xffffff   // 24 bits ����� handle ֻʹ���� ��  24 λ  ��  8 λ����Զ�̷���ʹ�� �����ڷֲ�ʽϵͳ�л��õ�
#define HANDLE_REMOTE_SHIFT 24 // Զ�� id ��Ҫƫ�� 24λ�õ�

// Զ�̷������Ͷ�Ӧ��handle
struct remote_name {
	char name[GLOBALNAME_LENGTH];
	uint32_t handle;
};

// Զ����Ϣ
struct remote_message {
	struct remote_name destination;
	const void * message;
	size_t sz;
};

// ��Զ�̷�������Ϣ
void skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session);

// ��masterע������� master����ͳһ�������Խڵ�
void skynet_harbor_register(struct remote_name *rname);

int skynet_harbor_message_isremote(uint32_t handle);

void skynet_harbor_init(int harbor);

int skynet_harbor_start(const char * master, const char *local);

#endif
