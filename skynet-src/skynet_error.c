#include "skynet.h"
#include "skynet_handle.h"
#include "skynet_mq.h"
#include "skynet_server.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define LOG_MESSAGE_SIZE 256 // ��־�Ĵ�С

// skynet �Դ�����ķ�װ

void 
skynet_error(struct skynet_context * context, const char *msg, ...) {
	static uint32_t logger = 0;
	if (logger == 0) {
		logger = skynet_handle_findname("logger"); // �������Ʋ���handle ���ҷ���
	}
	if (logger == 0) {
		return;
	}

	char tmp[LOG_MESSAGE_SIZE];
	char *data = NULL;

	va_list ap; // �ɱ����

	va_start(ap,msg);
	int len = vsnprintf(tmp, LOG_MESSAGE_SIZE, msg, ap); // vsnprintf() ���ɱ������ʽ�������һ���ַ����顣
	va_end(ap);

	if (len < LOG_MESSAGE_SIZE) {
		data = strdup(tmp); // strdup() �����������½���λ�ô� �õ�ʵ�ʵ� msg
	} else {
		int max_size = LOG_MESSAGE_SIZE;
		for (;;) {
			max_size *= 2;
			data = malloc(max_size); // msg ���� LOG_MESSAGE_SIZE ���� �������Ŀռ������

			va_start(ap,msg);
			len = vsnprintf(data, max_size, msg, ap); // �� msg ��ʽ���� data ��
			va_end(ap);

			if (len < max_size) { // ֪��д��  data �����ݲ��� max_size �� ʵ���Ͼ��� data�ܴ��msg
				break;
			}
			free(data);
		}
	}


	struct skynet_message smsg;
	if (context == NULL) {
		smsg.source = 0;
	} else {
		smsg.source = skynet_context_handle(context); // ctx->handle;
	}
	smsg.session = 0;
	smsg.data = data;
	smsg.sz = len | (PTYPE_TEXT << HANDLE_REMOTE_SHIFT);
	skynet_context_push(logger, &smsg); // ����Ϣ���͵���Ӧ�� handle �д���
}

