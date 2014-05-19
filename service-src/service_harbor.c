#include "skynet.h"
#include "skynet_harbor.h"
#include "skynet_socket.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>


// Harbor ��ͨ������� tcp ���ӹܵ��������ݣ���ɲ�ͬ�� skynet �ڵ������ݽ�����
// skynet Ŀǰ֧��һ��ȫ�����ַ��񣬿��԰�һ����Ϣ�����͵��ض����ֵķ����ϡ�������񲻱ش����ڵ�ǰ skynet �ڵ��С����������Ǿ���Ҫһ�������ܹ�ͬ����Щȫ�����֡�
// Ϊ�ˣ���ʵ����һ������ master �ķ����������þ��ǹ㲥ͬ�����е�ȫ�����֣��Լ���������� skynet �ڵ�ĵ�ַ�������ϣ���Щ��ַҲ��һ�����֡�
// ͬ�������� key-value ����ʽ���档����ÿ�� skynet �ڵ�Ŷ�Ӧһ���ַ����ĵ�ַ��

// http://blog.codingnow.com/2012/08/skynet_harbor_rpc.html

// harbor��Ҫ����skynet��Ⱥ ��ͬ�ڵ���ͨ�� ��skynet��Ⱥ��ͨ��ģ��

#define HASH_SIZE          4096
#define DEFAULT_QUEUE_SIZE 1024

struct msg {
	uint8_t * buffer; // buffer���������
	size_t size;
};

// mq
struct msg_queue {
	int size;
	int head;
	int tail;
	struct msg * data; // data[] slot�����˾�������ݶ�����ָ�� �����ʵ��Ϊ�̶���С�Ļ��λ����� ��С����ʱ��2��������
};

// key-value map ���� �� node�Ľṹ
// ����������ַ������� hash��ͻ���� �������ҵ� bucket���������в���
struct keyvalue {
	struct keyvalue * next;      // ����������ַ������� hash��ͻ���� �������ҵ� bucket���������в���
	char key[GLOBALNAME_LENGTH]; // value: name
	uint32_t hash;				 // hash
	uint32_t value;              // key  : handle
	struct msg_queue * queue; 	 // �������ÿ���ڵ㶼��һ����Ϣ����mq ���ڱ�������ڵ����Ϣ
};

// map map��ÿһ���ڵ㶼��һ����Ϣ����
struct hashmap {
	struct keyvalue *node[HASH_SIZE];
};

/*
	message type (8bits) is in destination high 8bits
	harbor id (8bits) is also in that place , but  remote message doesn't need harbor id.
 */
// ��8λ��Ŀ��id harbor idҲ����� ����Զ�̵�msg����Ҫharbor id ?

struct remote_message_header {
	uint32_t source;
	uint32_t destination;
	uint32_t session;
};

// harbor�Ľṹ harbor�����˱���Ⱥ���нڵ��ͨ�ŵ�ַ skynet��Ⱥ�ڲ������ n*n���ڵ� �൱��ÿ���ڵ�䶼������tcp����
struct harbor {
	struct skynet_context *ctx; 	// skynet_ctx
	char * local_addr;				// ���ص�ַ
	int id;                         // id
	struct hashmap * map;           // hashmap
	int master_fd;              	// master_fd
	char * master_addr;             // master_addr
	int remote_fd[REMOTE_MAX];  	// remote_fd[]
	bool connected[REMOTE_MAX];     // ��ʾremote_fd[]�Ƿ�������״̬
	char * remote_addr[REMOTE_MAX]; // remote_addr[]
};

// hash table

// ���еĲ���
// ---------------------
static void
_push_queue(struct msg_queue * queue, const void * buffer, size_t sz, struct remote_message_header * header) {
	// If there is only 1 free slot which is reserved to distinguish full/empty
	// of circular buffer, expand it.
	// ���ζ��� ���˾�����2��
	if (((queue->tail + 1) % queue->size) == queue->head) {
		struct msg * new_buffer = malloc(queue->size * 2 * sizeof(struct msg));
		int i;
		for (i=0;i<queue->size-1;i++) { // copy old data to new mem
			new_buffer[i] = queue->data[(i+queue->head) % queue->size];
		}

		free(queue->data);
		queue->data = new_buffer;
		queue->head = 0;
		queue->tail = queue->size - 1;
		queue->size *= 2;
	}

	struct msg * slot = &queue->data[queue->tail]; // �ҵ�������е�slot�������msg
	queue->tail = (queue->tail + 1) % queue->size;

	slot->buffer = malloc(sz + sizeof(*header));
	memcpy(slot->buffer, buffer, sz); // ��buffer������slot�� slot�Ǳ��������������� �����ǽ���ָ��
	memcpy(slot->buffer + sz, header, sizeof(*header));
	slot->size = sz + sizeof(*header);
}

static struct msg *
_pop_queue(struct msg_queue * queue) {
	if (queue->head == queue->tail) {
		return NULL;
	}

	struct msg * slot = &queue->data[queue->head];
	queue->head = (queue->head + 1) % queue->size; // ���ζ���
	return slot;
}

static struct msg_queue *
_new_queue() {
	struct msg_queue * queue = malloc(sizeof(*queue));
	queue->size = DEFAULT_QUEUE_SIZE;
	queue->head = 0;
	queue->tail = 0;
	queue->data = malloc(DEFAULT_QUEUE_SIZE * sizeof(struct msg)); // Ĭ�϶��д�СΪ1024�Ļ��ζ���

	return queue;
}

// �ͷŶ��������еĽڵ�
static void
_release_queue(struct msg_queue *queue) {
	if (queue == NULL)
		return;
	struct msg * m = _pop_queue(queue);
	while (m) {
		free(m->buffer);
		m = _pop_queue(queue);
	}

	free(queue->data);
	free(queue);
}

// hash�Ĳ���
// -------------------
// find name
static struct keyvalue *
_hash_search(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t*) name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue * node = hash->node[h % HASH_SIZE]; // ����������ַ������� hash��ͻ���� �������ҵ� bucket���������в���
	while (node) {
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			return node;
		}
		node = node->next;
	}
	return NULL;
}

/*

// Don't support erase name yet ��ʱ��֧��name�޳�

static struct void
_hash_erase(struct hashmap * hash, char name[GLOBALNAME_LENGTH) {
	uint32_t *ptr = name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
	struct keyvalue ** ptr = &hash->node[h % HASH_SIZE];
	while (*ptr) {
		struct keyvalue * node = *ptr;
		if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
			_release_queue(node->queue);
			*ptr->next = node->next;
			free(node);
			return;
		}
		*ptr = &(node->next);
	}
}
*/

// insert name to hash map
static struct keyvalue *
_hash_insert(struct hashmap * hash, const char name[GLOBALNAME_LENGTH]) {
	uint32_t *ptr = (uint32_t *)name;
	uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];

	struct keyvalue ** pkv = &hash->node[h % HASH_SIZE]; // ���bucket��ͷ
	struct keyvalue * node = malloc(sizeof(*node));      // �·���һ�� node ���� ��ֹ name

	memcpy(node->key, name, GLOBALNAME_LENGTH);
	node->next = *pkv;  // ͷ�巨����������
	node->queue = NULL;
	node->hash = h;
	node->value = 0;    // handleΪ 0
	*pkv = node;

	return node;
}

// new hash map
static struct hashmap * 
_hash_new() {
	struct hashmap * h = malloc(sizeof(struct hashmap));
	memset(h,0,sizeof(*h));
	return h;
}

// delete hash map and free map node
static void
_hash_delete(struct hashmap *hash) {
	int i;
	for (i=0;i<HASH_SIZE;i++) {
		struct keyvalue * node = hash->node[i];
		while (node) {
			struct keyvalue * next = node->next;
			_release_queue(node->queue); // �ͷŶ���
			free(node);
			node = next;
		}
	}
	free(hash);
}

// ----
// skynet harborģ��Ľӿ�
struct harbor *
harbor_create(void) {
	struct harbor * h = malloc(sizeof(*h));
	h->ctx = NULL;
	h->id = 0;
	h->master_fd = -1;
	h->master_addr = NULL;
	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		h->remote_fd[i] = -1;
		h->connected[i] = false;
		h->remote_addr[i] = NULL;
	}
	h->map = _hash_new(); // new hash map
	return h;
}

// harbor_release
void
harbor_release(struct harbor *h) {
	struct skynet_context *ctx = h->ctx;
	if (h->master_fd >= 0) {
		skynet_socket_close(ctx, h->master_fd);
	}
	free(h->master_addr);
	free(h->local_addr);

	int i;
	for (i=0;i<REMOTE_MAX;i++) {
		if (h->remote_fd[i] >= 0) {
			skynet_socket_close(ctx, h->remote_fd[i]);
			free(h->remote_addr[i]);
		}
	}

	_hash_delete(h->map);
	free(h);
}

// connect
static int
_connect_to(struct harbor *h, const char *ipaddress, bool blocking) {
	char * port = strchr(ipaddress,':'); // strchr����ԭ�ͣ�extern char *strchr(const char *s,char c);�����ַ���s���״γ����ַ�c��λ�á�
	if (port==NULL) {
		return -1;
	}

	int sz = port - ipaddress;
	char tmp[sz + 1];
	memcpy(tmp,ipaddress,sz); // tmp : ip
	tmp[sz] = '\0';

	int portid = strtol(port+1, NULL,10); // 10���Ƶ�port

	skynet_error(h->ctx, "Harbor(%d) connect to %s:%d", h->id, tmp, portid);

	// �������Ƿ����� connect masterʹ��������
	if (blocking) {
		return skynet_socket_block_connect(h->ctx, tmp, portid);
	}
	else {
		return skynet_socket_connect(h->ctx, tmp, portid);
	}
}

static inline void
to_bigendian(uint8_t *buffer, uint32_t n) {
	buffer[0] = (n >> 24) & 0xff;
	buffer[1] = (n >> 16) & 0xff;
	buffer[2] = (n >> 8) & 0xff;
	buffer[3] = n & 0xff;
}

static inline void
_header_to_message(const struct remote_message_header * header, uint8_t * message) {
	to_bigendian(message , header->source);
	to_bigendian(message+4 , header->destination);
	to_bigendian(message+8 , header->session);
}

static inline uint32_t
from_bigendian(uint32_t n) {
	union {
		uint32_t big;
		uint8_t bytes[4];
	} u;

	u.big = n; // y?
	return u.bytes[0] << 24 | u.bytes[1] << 16 | u.bytes[2] << 8 | u.bytes[3];
}

static inline void
_message_to_header(const uint32_t *message, struct remote_message_header *header) {
	header->source = from_bigendian(message[0]);
	header->destination = from_bigendian(message[1]);
	header->session = from_bigendian(message[2]);
}

// ������ȥ
static void
_send_package(struct skynet_context *ctx, int fd, const void * buffer, size_t sz) {
	uint8_t * sendbuf = malloc(sz+4);
	to_bigendian(sendbuf, sz);
	memcpy(sendbuf+4, buffer, sz); // ������ʱ�� ��ͷ��ѹ����4���ֽڵİ����С

	if (skynet_socket_send(ctx, fd, sendbuf, sz+4)) {
		skynet_error(ctx, "Send to %d error", fd);
	}
}

// ��Զ��fd������Ϣ
static void
_send_remote(struct skynet_context * ctx, int fd, const char * buffer, size_t sz, struct remote_message_header * cookie) {
	uint32_t sz_header = sz+sizeof(*cookie);
	uint8_t * sendbuf = malloc(sz_header+4);
	to_bigendian(sendbuf, sz_header);
	memcpy(sendbuf+4, buffer, sz);
	_header_to_message(cookie, sendbuf+4+sz);

	if (skynet_socket_send(ctx, fd, sendbuf, sz_header+4)) {
		skynet_error(ctx, "Remote send to %d error", fd);
	}
}

// harbor id[0, 255] ����Զ��harbor��address
static void
_update_remote_address(struct harbor *h, int harbor_id, const char * ipaddr) {
	if (harbor_id == h->id) {
		return;
	}

	assert(harbor_id > 0  && harbor_id< REMOTE_MAX);

	struct skynet_context * context = h->ctx;
	if (h->remote_fd[harbor_id] >=0) {
		skynet_socket_close(context, h->remote_fd[harbor_id]);
		free(h->remote_addr[harbor_id]);
		h->remote_addr[harbor_id] = NULL;
	}

	h->remote_fd[harbor_id] = _connect_to(h, ipaddr, false); // ���������� remote server
	h->connected[harbor_id] = false;
}

static void
_dispatch_queue(struct harbor *h, struct msg_queue * queue, uint32_t handle,  const char name[GLOBALNAME_LENGTH] ) {
	int harbor_id = handle >> HANDLE_REMOTE_SHIFT; // // Զ�� id ��Ҫƫ�� 24λ�õ� ��ʹ���˸�8λ
	assert(harbor_id != 0);
	struct skynet_context * context = h->ctx;

	int fd = h->remote_fd[harbor_id];
	if (fd < 0) {
		char tmp [GLOBALNAME_LENGTH+1];
		memcpy(tmp, name , GLOBALNAME_LENGTH);
		tmp[GLOBALNAME_LENGTH] = '\0';
		skynet_error(context, "Drop message to %s (in harbor %d)",tmp,harbor_id);
		return;
	}

	struct msg * m = _pop_queue(queue);
	while (m) {
		struct remote_message_header cookie;
		uint8_t *ptr = m->buffer + m->size - sizeof(cookie);
		memcpy(&cookie, ptr, sizeof(cookie));
		cookie.destination |= (handle & HANDLE_MASK);

		_header_to_message(&cookie, ptr); // cookie copy to ptr buffer

		_send_package(context, fd, m->buffer, m->size); // �������Ϣ���ͳ�ȥ ���͵���Ӧ��fd
		m = _pop_queue(queue); // ����������Ϣ
	}
}

static void
_update_remote_name(struct harbor *h, const char name[GLOBALNAME_LENGTH], uint32_t handle) {
	struct keyvalue * node = _hash_search(h->map, name); // �������Զ������
	if (node == NULL) {
		node = _hash_insert(h->map, name); // �����name[]���뵽hash_map��
	}

	node->value = handle;
	if (node->queue) {
		_dispatch_queue(h, node->queue, handle, name); // ������ڵ㱣�����Ϣ���ͳ�ȥ
		_release_queue(node->queue); // �ͷ�����ڵ㱣����Ϣ�Ķ���
		node->queue = NULL;
	}
}

// ����� handle����Ϣͬ����master��
static void
_request_master(struct harbor *h, const char name[GLOBALNAME_LENGTH], size_t i, uint32_t handle) {
	uint8_t buffer[4+i]; 		  // handle��uint32_t ������Ҫ4��uint8_t��������handle
	to_bigendian(buffer, handle); // bufferǰ32λ����handle
	memcpy(buffer+4,name,i);

	_send_package(h->ctx, h->master_fd, buffer, 4+i); // ����� handle�Ͷ�Ӧ��addr(ip:port)����Ϣͬ����master��
}

/*
	update global name to master

	2 bytes (size)
	4 bytes (handle) (handle == 0 for request)
	n bytes string (name)
 */

// ��ȫ�ֵ� master������½ڵ�name
static int
_remote_send_handle(struct harbor *h, uint32_t source, uint32_t destination, int type, int session, const char * msg, size_t sz) {
	int harbor_id = destination >> HANDLE_REMOTE_SHIFT;
	assert(harbor_id != 0);
	struct skynet_context * context = h->ctx;

	// ������Ϣ
	if (harbor_id == h->id) {
		// local message
		skynet_send(context, source, destination , type | PTYPE_TAG_DONTCOPY, session, (void *)msg, sz);
		return 1;
	}

	// Զ����Ϣ
	int fd = h->remote_fd[harbor_id];
	if (fd >= 0 && h->connected[harbor_id]) {
		struct remote_message_header cookie;
		cookie.source = source;
		cookie.destination = (destination & HANDLE_MASK) | ((uint32_t)type << HANDLE_REMOTE_SHIFT);
		cookie.session = (uint32_t)session;
		_send_remote(context, fd, msg,sz,&cookie);
	}
	else {
		// throw an error return to source
		if (session != 0) {
			skynet_send(context, destination, source, PTYPE_RESERVED_ERROR, session, NULL, 0);
		}
		skynet_error(context, "Drop message to harbor %d from %x to %x (session = %d, msgsz = %d)",harbor_id, source, destination,session,(int)sz);
	}
	return 0;
}

// ��masterע���µ�name��handle
static void
_remote_register_name(struct harbor *h, const char name[GLOBALNAME_LENGTH], uint32_t handle) {
	int i;
	for (i=0;i<GLOBALNAME_LENGTH;i++) {
		if (name[i] == '\0')
			break;
	}
	if (handle != 0) {
		_update_remote_name(h, name, handle); // ���� remote_name map
	}

	_request_master(h, name,i,handle); // ������ڵ���µ�master����
}

static int
_remote_send_name(struct harbor *h, uint32_t source, const char name[GLOBALNAME_LENGTH], int type, int session, const char * msg, size_t sz) {
	struct keyvalue * node = _hash_search(h->map, name);
	if (node == NULL) {
		node = _hash_insert(h->map, name);
	}

	if (node->value == 0) {
		if (node->queue == NULL) {
			node->queue = _new_queue();
		}
		struct remote_message_header header;
		header.source = source;
		header.destination = type << HANDLE_REMOTE_SHIFT;
		header.session = (uint32_t)session;
		_push_queue(node->queue, msg, sz, &header);
		// 0 for request

		// ��masterע���µ�name��handle
		_remote_register_name(h, name, 0);
		return 1;
	} else {
		return _remote_send_handle(h, source, node->value, type, session, msg, sz);
	}
}

static int
harbor_id(struct harbor *h, int fd) {
	int i;
	for (i=1;i<REMOTE_MAX;i++) {
		if (h->remote_fd[i] == fd)
			return i;
	}
	return 0; // ��Զ��fd��û�ҵ����fd ����0
}

static void
close_harbor(struct harbor *h, int fd) {
	int id = harbor_id(h,fd);
	if (id == 0)
		return;
	skynet_error(h->ctx, "Harbor %d closed",id);
	skynet_socket_close(h->ctx, fd); // �ر����socket fd
	h->remote_fd[id] = -1;
	h->connected[id] = false;
}

static void
open_harbor(struct harbor *h, int fd) {
	int id = harbor_id(h,fd);
	if (id == 0)
		return;
	assert(h->connected[id] == false);
	h->connected[id] = true;
}

static int
_mainloop(struct skynet_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct harbor * h = ud;
	switch (type) {

	// socket type
	case PTYPE_SOCKET: {
		const struct skynet_socket_message * message = msg;
		switch(message->type) {
		case SKYNET_SOCKET_TYPE_DATA:
			free(message->buffer);
			skynet_error(context, "recv invalid socket message (size=%d)", message->ud);
			break;
		case SKYNET_SOCKET_TYPE_ACCEPT:
			skynet_error(context, "recv invalid socket accept message");
			break;
		case SKYNET_SOCKET_TYPE_ERROR:
		case SKYNET_SOCKET_TYPE_CLOSE:   // socket����Ϊclose ��ʱ�� close harbor
			close_harbor(h, message->id);
			break;
		case SKYNET_SOCKET_TYPE_CONNECT: // connect ��ʱ�� open_harbor()
			open_harbor(h, message->id); // ������� ��ϢҪ���͵�fdΪtcp����״̬
			break;
		}
		return 0;
	}

	// harbor type ��Զ����Ϣ ����ĳ�� Զ������
	case PTYPE_HARBOR: {
		// remote message in
		const char * cookie = msg;
		cookie += sz - 12; // 3*4 = 12 sizeof(struct remote_message_header) = 12
		struct remote_message_header header;
		_message_to_header((const uint32_t *)cookie, &header);
		if (header.source == 0) {
			if (header.destination < REMOTE_MAX) { // Զ������hash_map���������¼�� ����������
				// 1 byte harbor id (0~255)
				// update remote harbor address
				char ip [sz - 11];
				memcpy(ip, msg, sz-12);
				ip[sz-11] = '\0';

				_update_remote_address(h, header.destination, ip); // harbor id[0, 255] ����Զ��harbor��address
			}
			else {
				// ������Զ������û�м��뵽 hash_map �й��� ������뵽 hash_map �������й���
				// update global name
				if (sz - 12 > GLOBALNAME_LENGTH) {
					char name[sz-11];
					memcpy(name, msg, sz-12);
					name[sz-11] = '\0';
					skynet_error(context, "Global name is too long %s", name);
				}

				_update_remote_name(h, msg, header.destination);
			}
		}
		// header.source != 0 ��һ��handle���͵�����һ��handle
		else {
			uint32_t destination = header.destination;
			int type = (destination >> HANDLE_REMOTE_SHIFT) | PTYPE_TAG_DONTCOPY;
			destination = (destination & HANDLE_MASK) | ((uint32_t)h->id << HANDLE_REMOTE_SHIFT);

			skynet_send(context, header.source, destination, type, (int)header.session, (void *)msg, sz-12);
			return 1;
		}
		return 0;
	}

	// system type
	case PTYPE_SYSTEM: {
		// register name message
		const struct remote_message *rmsg = msg;
		assert (sz == sizeof(rmsg->destination));
		_remote_register_name(h, rmsg->destination.name, rmsg->destination.handle); // ע��name�Ͷ�Ӧ��handle
		return 0;
	}

	// other remote msg out
	default: {
		// remote message out
		const struct remote_message *rmsg = msg;
		if (rmsg->destination.handle == 0) {
			if (_remote_send_name(h, source , rmsg->destination.name, type, session, rmsg->message, rmsg->sz)) {
				return 0;
			}
		} else {
			if (_remote_send_handle(h, source , rmsg->destination.handle, type, session, rmsg->message, rmsg->sz)) {
				return 0;
			}
		}
		free((void *)rmsg->message);
		return 0;
	}
	}
}

static void
_launch_gate(struct skynet_context * ctx, const char * local_addr) {
	char tmp[128];
	sprintf(tmp,"gate L ! %s %d %d 0",local_addr, PTYPE_HARBOR, REMOTE_MAX);

	// �õ�gate�����handle
	const char * gate_addr = skynet_command(ctx, "LAUNCH", tmp);
	if (gate_addr == NULL) {
		fprintf(stderr, "Harbor : launch gate failed\n");
		exit(1);
	}

	uint32_t gate = strtoul(gate_addr+1 , NULL, 16);
	if (gate == 0) {
		fprintf(stderr, "Harbor : launch gate invalid %s", gate_addr);
		exit(1);
	}

	// �õ������handle
	const char * self_addr = skynet_command(ctx, "REG", NULL);
	int n = sprintf(tmp,"broker %s",self_addr);

	// send to ����ʹ�õļ��ı�Э�� PTYPE_TEXT
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, tmp, n);
	skynet_send(ctx, 0, gate, PTYPE_TEXT, 0, "start", 5);
}

// harbor module init ����
int
harbor_init(struct harbor *h, struct skynet_context *ctx, const char * args) {
	h->ctx = ctx;
	int sz = strlen(args)+1; // inlcude \0
	char master_addr[sz];
	char local_addr[sz];
	int harbor_id = 0;

	sscanf(args,"%s %s %d",master_addr, local_addr, &harbor_id); // sscanf()��args��������ݸ�ʽ���������3��ֵ��
	h->master_addr = strdup(master_addr);
	// strdup()�����������½���λ�ô�
	// strdup()���ڲ�������malloc()Ϊ���������ڴ棬����Ҫʹ�÷��ص��ַ���ʱ����Ҫ��free()�ͷ���Ӧ���ڴ�ռ䣬���������ڴ�й©��

	h->master_fd = _connect_to(h, master_addr, true); // �������� master������
	if (h->master_fd == -1) {
		fprintf(stderr, "Harbor: Connect to master failed\n");
		exit(1);
	}
	h->local_addr = strdup(local_addr);
	h->id = harbor_id;

	// ����gate���� ��tcp����
	_launch_gate(ctx, local_addr);
	skynet_callback(ctx, h, _mainloop); // �������ģ��Ļص�����
	_request_master(h, local_addr, strlen(local_addr), harbor_id); // ��masterͬ�����handle��harbor_id

	return 0;
}
