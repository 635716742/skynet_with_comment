#ifndef SKYNET_LOCAL_CAST_H
#define SKYNET_LOCAL_CAST_H

#include <stdint.h>

// ���ع㲥�Ľṹ
struct localcast {
	int n;
	const uint32_t * group;
	void *msg;
	size_t sz;
};

#endif
