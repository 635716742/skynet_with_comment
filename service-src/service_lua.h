#ifndef SKYNET_SERVICE_LUA_H
#define SKYNET_SERVICE_LUA_H

// snlua�ṹ ����lua���� ÿһ��lua����ʵ������ lua��ķ��� + c���snlua����

struct snlua {
	lua_State * L;
	const char * reload;
	struct skynet_context * ctx;
	struct tqueue * tq;
	int (*init)(struct snlua *l, struct skynet_context *ctx, const char * args);
};

#endif
