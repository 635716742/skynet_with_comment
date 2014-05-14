#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_server.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>

// harbor ������Զ������ͨ�� master ͳһ������
// http://blog.codingnow.com/2012/09/the_design_of_skynet.html
// ����� skynet��������� ������ session�� type������

static struct skynet_context * REMOTE = 0;		// harbor �����Ӧ�� skynet_context ָ��
static unsigned int HARBOR = 0;

void 
skynet_harbor_send(struct remote_message *rmsg, uint32_t source, int session) {
	int type = rmsg->sz >> HANDLE_REMOTE_SHIFT; // ��  8 bite ���ڱ��� type
	rmsg->sz &= HANDLE_MASK;
	assert(type != PTYPE_SYSTEM && type != PTYPE_HARBOR);
	skynet_context_send(REMOTE, rmsg, sizeof(*rmsg) , source, type , session);
}

// ��  master ע��
void 
skynet_harbor_register(struct remote_name *rname) {
	int i;
	int number = 1;
	for (i=0; i<GLOBALNAME_LENGTH; i++) {
		char c = rname->name[i];
		if (!(c >= '0' && c <='9')) { // ȷ��Զ�����������ڲ���0-9 ��Χ��
			number = 0;
			break;
		}
	}

	assert(number == 0);
	skynet_context_send(REMOTE, rname, sizeof(*rname), 0, PTYPE_SYSTEM , 0);
}

int 
skynet_harbor_message_isremote(uint32_t handle) { // �ж���Ϣ�ǲ�������Զ��������
	int h = (handle & ~HANDLE_MASK); // ȡ��8λ
	return h != HARBOR && h !=0;
}

void
skynet_harbor_init(int harbor) {
	HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT; // ��8λ���Ƕ�ӦԶ������ͨ�ŵ� harbor
}

int
skynet_harbor_start(const char * master, const char *local) {
	size_t sz = strlen(master) + strlen(local) + 32;
	char args[sz];
	sprintf(args, "%s %s %d",master,local,HARBOR >> HANDLE_REMOTE_SHIFT);
	struct skynet_context * inst = skynet_context_new("harbor",args);
	if (inst == NULL) {
		return 1;
	}
	REMOTE = inst;

	return 0;
}
