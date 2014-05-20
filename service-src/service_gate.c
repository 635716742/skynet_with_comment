#include "skynet.h"
#include "skynet_socket.h"
#include "databuffer.h"
#include "hashid.h"

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#define BACKLOG 32

// ÿ��master����    ʵ������   master���� + gate�����ܺ�
// ÿ��harbor����ʵ������   harbor����+gate�����ܺ�
// gate��connection
// gate��������skynet�����TCPͨ�� �����ⲿ����Ϣ��ʽת����skynet�ڲ�����Ϣ

#if 0

	Gate �� Connection

	�����ᵽ�Ķ��� skynet �ڲ���Э�����ơ���һ����������Ϸ���������ⲻ�غ����ͨѶ��
	���ͨѶ�����֣�һ����Ϸ�ͻ���ʹ�� TCP ���ӽ��� skynet �ڵ㡣��������Ϸ�����ģ��ǻ����Ƕȿ���
	������� skynet ʵ��һ�� web �������Ļ�����Ϸ�ͻ��˾Ϳ��Եȼ���һ�����������
	��һ���ǵ������ķ��񣬱������ݿ����������һ������ TCP ���ӡ�����Ҫ�� skynet �ڲ�����һ�� TCP ���ӳ�ȥʹ�á�
	��Ȼ����ȫ���Ա�дһ���� skynet �ӿڹ淶ʵ�ֵ����ݿ⣬�ǻ����Ч������ʵ�п��º��������������Ľ�����ʵ��һ���ڴ� cache ���ѡ�
	�����磬�����˲��� 10 �� lua ���룬ʵ����һ���򵥵� key-value �Ľ����ڴ����ݿ�ķ�����
	ǰ�ߣ��ҳ�Ϊ gate �������������Ǽ���һ�� TCP �˿ڣ���������� TCP ���ӣ����������ϻ�õ�����ת���� skynet �ڲ���
	Gate �������������ⲿ���ݰ��� skynet �ڲ���Ϣ���Ĳ�һ���ԡ��ⲿ TCP ���ķְ����⣬�� Gate ʵ���ϵ�Լ������ʵ����һ�� gate ����
	�������ֽڵĴ�ͷ�ֽ�������ʾһ���ְ����ȡ����ģ�������ǰ��ʱ���һ������Ŀ ���������ҿ���ʵ�ֵĸ�Ϊͨ�ã�
	����֧�ֿ����õķְ�������Erlang ���ⷽ�����ĺ�ȫ�棩�����Ҹ��뱣�ִ���ľ���
	��Ȼ������� skynet ȥʵ��һ��ͨ�õ� web server ����� gate �Ͳ�̫�����ˡ�
	����дһ�����Ƶ� Gate ���񲢲����ѡ�Ϊ web server ����һ�� gate �������򵥣���Ϊ������Ҫ�ְ��ˡ�

	Gate ������ⲿ���ӣ��������������Ϣת������һ������ȥ�������Լ��������ݴ�������Ϊ������Ҫ���� gate ʵ�ֵļ���Ч��
	C ��������ʤ����������������������ҵ���߼�������أ����ǿ����� Lua ��ɡ�

	�ⲿ��Ϣ�����࣬һ�������ӱ���Ľ���ͶϿ���Ϣ����һ���������ϵ����ݰ���һ��ʼ��Gate ������ת����������Ϣ��ͬһ���������
	�������������ݰ������һ����ͷ�����������ϵĿ��������� Gate ��������һ�ֹ���ģʽ����ÿ����ͬ�����ϵ����ݰ�ת������ͬ�Ķ��������ϡ�
	ÿ������������һ�����ϵ����ݰ���

	���ߣ�����Ҳ����ѡ��Ѳ�ͬ�����ϵ����ݰ��ӿ�����Ϣ��������/�Ͽ����ӣ��з��뿪��
	�������ֲ�ͬ���Ӷ�ת����ͬһ���ݴ�����񣨶�������Դ�����У�ֻ�������������еĳ��ϣ���

	������ģʽ���ҷֱ��Ϊ watchdog ģʽ���� gate ���ϰ�ͷ��ͬʱ���������Ϣ��������Ϣ���������ݣ�agent ģʽ��
	��ÿ�� agent ����������ӣ��Լ� broker ģʽ����һ�� broker ������ͬ�����ϵ��������ݰ�������������ģʽ��
	������Ϣ���ǽ��� watchdog ȥ����ģ������ݰ���������� watchdog ���Ƿ��͸� agent �� broker �Ļ����򲻻��ж��������ͷ��Ҳ���������ݿ�������
	ʶ����Щ���Ǵ��ⲿ���ͽ����ķ����Ǽ����Ϣ���������Ƿ�Ϊ PTYPE_CLIENT ����Ȼ����Ҳ�����Լ�������Ϣ������ gate ֪ͨ�㡣

	Skynet �Ļ��������У����ڼ�Ⱥ��ͨѶ���ǲ��֣��Ѿ������� gate ģ����Ϊʵ�ֵ�һ���֡����� gate ģ����һ������� skynet ���������
	��ʹ���� skynet ����� api ����û���漰 skynet �ڲ����κ�ϸ�ڡ��� Harbor ģ��ʹ�� gate ʱ�����õ� broker ģ�飬�Ҷ�������Ϣ������Ϊ PTYPE_HARBOR ��

	�ڿ�Դ��Ŀ��ʾ�������У����ǻ�������һ���򵥵� gate �����Լ���Ӧ�� watchdog �� agent �������ø����� client ����������ȥ��
	ͨ���ı�Э��� skynet ���н�����agent ��ת�����е� client ����� skynet �ڲ��� simpledb ����simpledb ��һ�����׵� key-value �ڴ����ݿ⡣
	�������� client �Ϳ��������������ݿ��ѯ�͸��²����ˡ�

	ע�⣬Gate ֻ�����ȡ�ⲿ���ݣ����������д��Ҳ����˵������Щ���ӷ������ݲ�������ְ�𷶳롣
	��Ϊʾ����skynet ��Դ��Ŀʵ����һ���򵥵Ļ�д������񣬽��� service_client �����������������ʱ��һ�� fd ��
	���͸�����������Ϣ�������ᱻ�������ֽڵĳ��Ȱ�ͷ��д����Ӧ�� fd �����ݲ�ͬ�ķְ�Э�飬�����Լ����Ʋ�ͬ�� client ������������ⲿ���ӷ������ݵ�ģ�顣

#endif // 0

// 1.watchdog ģʽ���� gate ���ϰ�ͷ��ͬʱ���������Ϣ��������Ϣ���������ݣ�
// 2.agent    ģʽ����ÿ�� agent ����������ӣ�
// 3.broker   ģʽ����һ�� broker ������ͬ�����ϵ��������ݰ���
// ����������ģʽ��������Ϣ���ǽ��� watchdog ȥ����ģ������ݰ���������� watchdog ���Ƿ��͸� agent �� broker �Ļ���
// �򲻻��ж��������ͷ��Ҳ���������ݿ�������ʶ����Щ���Ǵ��ⲿ���ͽ����ķ����Ǽ����Ϣ���������Ƿ�Ϊ PTYPE_CLIENT ����Ȼ����Ҳ�����Լ�������Ϣ������ gate ֪ͨ�㡣

// connection�ṹ�����˿ͻ��˵�������Ϣ
struct connection {
	int 				id;					// skynet_socket id skynetӦ�ò���һ��socket��
	uint32_t 			agent;
	uint32_t 			client;
	char 				remote_name[32];
	struct databuffer 	buffer;
};

// ����� tcp����
struct gate {
	struct skynet_context  *ctx;
	int 					listen_id; 		 // listen fd
	uint32_t 				watchdog;        // watchdog handle
	uint32_t 				broker;          // handle
	int 					client_tag;
	int 					header_size;
	int 					max_connection;
	struct 					hashid hash;
	struct 					connection *conn; // �ͻ�������fd�ı���

	// todo: save message pool ptr for release
	struct messagepool 		mp;
};

struct gate *
gate_create(void) {
	struct gate * g = malloc(sizeof(*g));
	memset(g,0,sizeof(*g));
	g->listen_id = -1;
	return g;
}

void
gate_release(struct gate *g) {
	int i;
	struct skynet_context *ctx = g->ctx;
	for (i=0;i<g->max_connection;i++) {
		struct connection *c = &g->conn[i];
		if (c->id >=0) {
			skynet_socket_close(ctx, c->id); // �����رպͿͻ��˵�����
		}
	}

	if (g->listen_id >= 0) {
		skynet_socket_close(ctx, g->listen_id);
	}

	messagepool_free(&g->mp);
	hashid_clear(&g->hash);
	free(g->conn);
	free(g);
}

static void
_parm(char *msg, int sz, int command_sz) {
	while (command_sz < sz) {
		if (msg[command_sz] != ' ')
			break;
		++command_sz;
	}
	int i;
	for (i=command_sz;i<sz;i++) {
		msg[i-command_sz] = msg[i];
	}
	msg[i-command_sz] = '\0';
}

static void
_forward_agent(struct gate * g, int fd, uint32_t agentaddr, uint32_t clientaddr) {
	int id = hashid_lookup(&g->hash, fd);
	if (id >=0) {
		struct connection * agent = &g->conn[id];
		agent->agent = agentaddr;
		agent->client = clientaddr;
	}
}

// ��������Ĵ���
static void
_ctrl(struct gate * g, const void * msg, int sz) {
	struct skynet_context * ctx = g->ctx;
	char tmp[sz+1];
	memcpy(tmp, msg, sz);
	tmp[sz] = '\0';
	char * command = tmp;

	int i;
	if (sz == 0)
		return;

	for (i=0;i<sz;i++) {
		if (command[i]==' ') {
			break;
		}
	}

	// kick �ߵ�
	if (memcmp(command,"kick",i)==0) {
		_parm(tmp, sz, i);
		int uid = strtol(command , NULL, 10);
		int id = hashid_lookup(&g->hash, uid);
		if (id>=0) {
			skynet_socket_close(ctx, uid);
		}
		return;
	}

	// forward ��ǰ������Ϣ
	if (memcmp(command,"forward",i)==0) {
		_parm(tmp, sz, i);
		char * client = tmp;
		char * idstr = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		int id = strtol(idstr , NULL, 10);
		char * agent = strsep(&client, " ");
		if (client == NULL) {
			return;
		}
		uint32_t agent_handle = strtoul(agent+1, NULL, 16);
		uint32_t client_handle = strtoul(client+1, NULL, 16);
		_forward_agent(g, id, agent_handle, client_handle);
		return;
	}

	// broker
	if (memcmp(command,"broker",i)==0) {
		_parm(tmp, sz, i);
		g->broker = skynet_queryname(ctx, command); // handle
		return;
	}

	// start
	if (memcmp(command,"start",i) == 0) {
		skynet_socket_start(ctx, g->listen_id);
		return;
	}

	// close
    if (memcmp(command, "close", i) == 0) {
		if (g->listen_id >= 0) {
			skynet_socket_close(ctx, g->listen_id);
			g->listen_id = -1;
		}
		return;
	}
	skynet_error(ctx, "[gate] Unkown command : %s", command);
}

// ���棿 ���͵��ǿ�������
static void
_report(struct gate * g, const char * data, ...) {
	if (g->watchdog == 0) {
		return;
	}

	struct skynet_context * ctx = g->ctx;
	va_list ap;
	va_start(ap, data);
	char tmp[1024];
	int n = vsnprintf(tmp, sizeof(tmp), data, ap);
	va_end(ap);

	skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT,  0, tmp, n);
}

static void
_forward(struct gate *g, struct connection * c, int size) {
	struct skynet_context * ctx = g->ctx;
	if (g->broker) {
		void * temp = malloc(size);
		databuffer_read(&c->buffer,&g->mp,temp, size);
		skynet_send(ctx, 0, g->broker, g->client_tag | PTYPE_TAG_DONTCOPY, 0, temp, size);
		return;
	}
	if (c->agent) {
		void * temp = malloc(size);
		databuffer_read(&c->buffer,&g->mp,temp, size);
		skynet_send(ctx, c->client, c->agent, g->client_tag | PTYPE_TAG_DONTCOPY, 0 , temp, size);
	} else if (g->watchdog) {
		char * tmp = malloc(size + 32);
		int n = snprintf(tmp,32,"%d data ",c->id);
		databuffer_read(&c->buffer,&g->mp,tmp+n,size);
		skynet_send(ctx, 0, g->watchdog, PTYPE_TEXT | PTYPE_TAG_DONTCOPY, 0, tmp, size + n);
	}
}

// �ַ���Ϣ
static void
dispatch_message(struct gate *g, struct connection *c, int id, void * data, int sz) {
	databuffer_push(&c->buffer,&g->mp, data, sz);
	for (;;) {
		int size = databuffer_readheader(&c->buffer, &g->mp, g->header_size);
		if (size < 0) {
			return;
		}
		else if (size > 0) {
			if (size >= 0x1000000) {
				struct skynet_context * ctx = g->ctx;
				databuffer_clear(&c->buffer,&g->mp);
				skynet_socket_close(ctx, id);
				skynet_error(ctx, "Recv socket message > 16M");
				return;
			}
			else {
				_forward(g, c, size);
				databuffer_reset(&c->buffer);
			}
		}
	}
}

// socket��Ϣ�Ĵ���
static void
dispatch_socket_message(struct gate *g, const struct skynet_socket_message * message, int sz) {
	struct skynet_context * ctx = g->ctx;
	switch(message->type) {

	// data
	case SKYNET_SOCKET_TYPE_DATA: {
		int id = hashid_lookup(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			dispatch_message(g, c, message->id, message->buffer, message->ud);
		}
		else {
			skynet_error(ctx, "Drop unknown connection %d message", message->id);
			skynet_socket_close(ctx, message->id);
			free(message->buffer);
		}
		break;
	}

	// connect
	case SKYNET_SOCKET_TYPE_CONNECT: {
		if (message->id == g->listen_id) {
			// start listening
			break;
		}
		int id = hashid_lookup(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			_report(g, "%d open %d %s:0",message->id,message->id,c->remote_name);
		}
		else {
			skynet_error(ctx, "Close unknown connection %d", message->id);
			skynet_socket_close(ctx, message->id);
		}
		break;
	}

	// close and error
	case SKYNET_SOCKET_TYPE_CLOSE:
	case SKYNET_SOCKET_TYPE_ERROR: {
		int id = hashid_remove(&g->hash, message->id);
		if (id>=0) {
			struct connection *c = &g->conn[id];
			databuffer_clear(&c->buffer,&g->mp);
			memset(c, 0, sizeof(*c));
			c->id = -1;
			_report(g, "%d close", message->id);
		}
		break;
	}

	// accept
	case SKYNET_SOCKET_TYPE_ACCEPT:
		// report accept, then it will be get a SKYNET_SOCKET_TYPE_CONNECT message
		assert(g->listen_id == message->id);
		if (hashid_full(&g->hash)) {
			skynet_socket_close(ctx, message->ud);
		} else {
			struct connection *c = &g->conn[hashid_insert(&g->hash, message->ud)];
			if (sz >= sizeof(c->remote_name)) {
				sz = sizeof(c->remote_name) - 1;
			}
			c->id = message->ud;
			memcpy(c->remote_name, message+1, sz);
			c->remote_name[sz] = '\0';
			skynet_socket_start(ctx, message->ud);
		}
		break;
	}
}

// gate module callback()
static int
_cb(struct skynet_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
	struct gate *g = ud;
	switch(type) {

	// skynet�ڲ����ı�Э�� һ����˵�ǿ�������
	case PTYPE_TEXT:
		_ctrl(g , msg , (int)sz);
		break;

	// �ͻ��˵���Ϣ
	case PTYPE_CLIENT: {
		if (sz <=4 ) {
			skynet_error(ctx, "Invalid client message from %x",source);
			break;
		}

		// The last 4 bytes in msg are the id of socket, write following bytes to it
		// msg�ĺ�4���ֽ���socket��id ֮����ʣ�µ��ֽ�
		const uint8_t * idbuf = msg + sz - 4;
		uint32_t uid = idbuf[0] | idbuf[1] << 8 | idbuf[2] << 16 | idbuf[3] << 24;

		// �ҵ����socket id����Ӧ�ò�ά����socket fd
		int id = hashid_lookup(&g->hash, uid);
		if (id>=0) {
			// don't send id (last 4 bytes)
			skynet_socket_send(ctx, uid, (void*)msg, sz-4);

			// return 1 means don't free msg
			return 1;
		}
		else {
			skynet_error(ctx, "Invalid client id %d from %x",(int)uid,source);
			break;
		}
	}

	// socket����Ϣ���� �ַ���Ϣ
	case PTYPE_SOCKET:
		assert(source == 0);
		// recv socket message from skynet_socket
		dispatch_socket_message(g, msg, (int)(sz-sizeof(struct skynet_socket_message)));
		break;
	}
	return 0;
}

static int
start_listen(struct gate *g, char * listen_addr) {
	struct skynet_context * ctx = g->ctx;
	char * portstr = strchr(listen_addr,':');
	const char * host = "";
	int port;
	if (portstr == NULL) {
		port = strtol(listen_addr, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return 1;
		}
	} else {
		port = strtol(portstr + 1, NULL, 10);
		if (port <= 0) {
			skynet_error(ctx, "Invalid gate address %s",listen_addr);
			return 1;
		}
		portstr[0] = '\0';
		host = listen_addr;
	}

	// socket_listen() to client connect
	g->listen_id = skynet_socket_listen(ctx, host, port, BACKLOG);
	if (g->listen_id < 0) {
		return 1;
	}
	return 0;
}

// gate_init()
int
gate_init(struct gate *g , struct skynet_context * ctx, char * parm) {
	if (parm == NULL)
		return 1;

	int max = 0; // max connext num
	int buffer = 0;
	int sz = strlen(parm)+1;

	char watchdog[sz];
	char binding[sz]; // binding: listen_addr

	int client_tag = 0;
	char header;

	int n = sscanf(parm, "%c %s %s %d %d %d",&header,watchdog, binding,&client_tag , &max,&buffer);
	if (n<4) {
		skynet_error(ctx, "Invalid gate parm %s",parm);
		return 1;
	}

	if (max <=0 ) {
		skynet_error(ctx, "Need max connection");
		return 1;
	}

	// header S����L��ͷ��
	if (header != 'S' && header !='L') {
		skynet_error(ctx, "Invalid data header style");
		return 1;
	}

	// PTYPE_CLIENT �ͻ�����Ϣ
	if (client_tag == 0) {
		client_tag = PTYPE_CLIENT;
	}

	if (watchdog[0] == '!') {
		g->watchdog = 0;
	}
	else {
		g->watchdog = skynet_queryname(ctx, watchdog);
		if (g->watchdog == 0) {
			skynet_error(ctx, "Invalid watchdog %s",watchdog);
			return 1;
		}
	}

	g->ctx = ctx;

	hashid_init(&g->hash, max); // hashid_intit()

	g->conn = malloc(max * sizeof(struct connection));
	memset(g->conn, 0, max *sizeof(struct connection));
	g->max_connection = max;

	int i;
	for (i=0;i<max;i++) {
		g->conn[i].id = -1;
	}
	
	g->client_tag = client_tag;
	g->header_size = (header=='S') ? 2 : 4;

	skynet_callback(ctx,g,_cb); // �������ģ��Ļص�����

	return start_listen(g, binding); // ��ʼ�����ͻ��˵����� binding:listen_addr
}
