#include "skynet_handle.h"
#include "skynet_server.h"
#include "rwlock.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#define DEFAULT_SLOT_SIZE 4

// skynet �����ŵĹ���ͷ���

// handle_name ���� name �� ��Ӧ handle id �� hash��
struct handle_name {
	char * name;
	uint32_t handle;
};

// �洢handle��skynet_context�Ķ�Ӧ��ϵ����һ����ϣ��
// ÿ������skynet_context����Ӧһ�����ظ���handle
// ͨ��handle��ɻ�ȡskynet_context
// ������ handle �� skynet_context�Ķ�Ӧ
struct handle_storage {
	struct rwlock lock;				// ��д��

	uint32_t harbor;				// �������� harbor harbor ���ڲ�ͬ������ͨ��
	uint32_t handle_index;			// ��ʼֵΪ1����ʾhandle�����ʼֵ��1��ʼ
	int slot_size;					// hash ��ռ��С����ʼΪDEFAULT_SLOT_SIZE
	struct skynet_context ** slot;	// skynet_context ��ռ�
	
	int name_cap;	// handle_name��������ʼΪ2������ name_cap �� slot_size ��һ����ԭ�����ڣ�����ÿ�� handle ����name
	int name_count;				 // handle_name��
	struct handle_name *name;	 // handle_name��
};

static struct handle_storage *H = NULL;

// ע��ctx���� ctx �浽 handle_storage ��ϣ���У����õ�һ��handle
uint32_t
skynet_handle_register(struct skynet_context *ctx) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);
	
	for (;;) {
		int i;
		for (i=0; i<s->slot_size; i++) {
			uint32_t handle = (i+s->handle_index) & HANDLE_MASK;
			int hash = handle & (s->slot_size-1);	// �ȼ��� handle % s->slot_size
			if (s->slot[hash] == NULL) { // �ҵ�δʹ�õ�  slot ����� ctx ������� slot ��
				s->slot[hash] = ctx;
				s->handle_index = handle + 1; // �ƶ� handle_index �����´�ʹ��

				rwlock_wunlock(&s->lock);

				handle |= s->harbor; // harbor ���ڲ�ͬ�������ͨ�� handle��8λ����harbor ��24λ���ڱ����� ����������Ҫ |= ��
				skynet_context_init(ctx, handle); // ���� ctx->handle = handle; ����� ctx �� handle
				return handle;
			}
		}
		assert((s->slot_size*2 - 1) <= HANDLE_MASK); // ȷ�� ����2���ռ�� �ܹ�handle�� slot������������ 24λ������

		// ��ϣ������2��
		struct skynet_context ** new_slot = malloc(s->slot_size * 2 * sizeof(struct skynet_context *));
		memset(new_slot, 0, s->slot_size * 2 * sizeof(struct skynet_context *));

		// ��ԭ�������ݿ������µĿռ�
		for (i=0;i<s->slot_size;i++) {
			int hash = skynet_context_handle(s->slot[i]) & (s->slot_size * 2 - 1); // ӳ���µ� hash ֵ
			assert(new_slot[hash] == NULL);
			new_slot[hash] = s->slot[i];
		}

		free(s->slot); // free old mem
		s->slot = new_slot;
		s->slot_size *= 2;
	}
}

// �ջ�handle
void
skynet_handle_retire(uint32_t handle) {
	struct handle_storage *s = H;

	rwlock_wlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1); // �ȼ���  handle % s->slot_size
	struct skynet_context * ctx = s->slot[hash];

	if (ctx != NULL && skynet_context_handle(ctx) == handle) {
		skynet_context_release(ctx); // free skynet_ctx
		s->slot[hash] = NULL;		// �ÿգ���ϣ���ڳ��ռ�

		int i;
		int j=0,
			n=s->name_count;
		for (i=0; i<n; ++i) {
			if (s->name[i].handle == handle) { // �� name ���� �ҵ� handle ��Ӧ�� name free��
				free(s->name[i].name);
				continue;
			} else if (i!=j) {	// ˵��free��һ��name
				s->name[j] = s->name[i];	// �����Ҫ������Ԫ���Ƶ�ǰ��
			}
			++j;
		}
		s->name_count = j;
	}

	rwlock_wunlock(&s->lock);
}

// �ջ�����handle
void 
skynet_handle_retireall() {
	struct handle_storage *s = H;

	for (;;) {
		int n=0;
		int i;
		for (i=0;i<s->slot_size;i++) {
			rwlock_rlock(&s->lock);
			struct skynet_context * ctx = s->slot[i];
			rwlock_runlock(&s->lock);
			if (ctx != NULL) {
				++n;
				skynet_handle_retire(skynet_context_handle(ctx));
			}
		}
		if (n==0)
			return;
	}
}

// ͨ��handle��ȡskynet_context, skynet_context�����ü�����1
struct skynet_context * 
skynet_handle_grab(uint32_t handle) {
	struct handle_storage *s = H;
	struct skynet_context * result = NULL;

	rwlock_rlock(&s->lock);

	uint32_t hash = handle & (s->slot_size-1);
	struct skynet_context * ctx = s->slot[hash];
	if (ctx && skynet_context_handle(ctx) == handle) {
		result = ctx;
		skynet_context_grab(result); // 	__sync_add_and_fetch(&ctx->ref,1);	skynet_context���ü�����1
	}

	rwlock_runlock(&s->lock);

	return result;
}

// �������Ʋ���handle
uint32_t 
skynet_handle_findname(const char * name) {
	struct handle_storage *s = H;

	rwlock_rlock(&s->lock);

	uint32_t handle = 0;

	int begin = 0;
	int end = s->name_count - 1;
	while (begin<=end) {
		int mid = (begin+end)/2; // �����õĶ��ֲ��� ��˵����������� �ĳɼ������Ա��� �����������ô��
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name); // // һֱ��ͷ������ ʵ������������� name �ᰴ���ȴ�С�ź��� ��������ʹ�ö��ֲ�����
		if (c==0) {
			handle = n->handle;
			break;
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}

	rwlock_runlock(&s->lock);

	return handle;
}

static void
_insert_name_before(struct handle_storage *s, char *name, uint32_t handle, int before) {
	if (s->name_count >= s->name_cap) {
		s->name_cap *= 2;
		struct handle_name * n = malloc(s->name_cap * sizeof(struct handle_name));
		int i;
		for (i=0;i<before;i++) {
			n[i] = s->name[i];
		}
		for (i=before;i<s->name_count;i++) {
			n[i+1] = s->name[i];
		}
		free(s->name);
		s->name = n;
	} else {
		int i;
		// before֮���Ԫ�غ���һ��λ��
		for (i=s->name_count;i>before;i--) {
			s->name[i] = s->name[i-1];
		}
	}
	s->name[before].name = name;
	s->name[before].handle = handle;
	s->name_count ++;
}

// ���� name �� handle
static const char *
_insert_name(struct handle_storage *s, const char * name, uint32_t handle) {
	int begin = 0;
	int end = s->name_count - 1;
	// ���ֲ���
	while (begin<=end) {
		int mid = (begin+end)/2;
		struct handle_name *n = &s->name[mid];
		int c = strcmp(n->name, name);
		if (c==0) {
			return NULL;	// �����Ѵ��� �������Ʋ����ظ�����
		}
		if (c<0) {
			begin = mid + 1;
		} else {
			end = mid - 1;
		}
	}
	char * result = strdup(name);

	_insert_name_before(s, result, handle, begin); // һֱ��ͷ������ ʵ������������� name �ᰴ���ȴ�С�ź��� ��������ʹ�ö��ֲ�����

	return result;
}

// name��handle��
// ������ע��һ�����Ƶ�ʱ����õ��ú���
const char * 
skynet_handle_namehandle(uint32_t handle, const char *name) {
	rwlock_wlock(&H->lock);

	const char * ret = _insert_name(H, name, handle);

	rwlock_wunlock(&H->lock);

	return ret;
}

// ��ʼ��һ�� handle ���ǳ�ʼ�� handle_storage
void 
skynet_handle_init(int harbor) {
	assert(H==NULL);

	struct handle_storage * s = malloc(sizeof(*H));
	s->slot_size = DEFAULT_SLOT_SIZE;	// ��ռ��ʼΪDEFAULT_SLOT_SIZE
	s->slot = malloc(s->slot_size * sizeof(struct skynet_context *)); // Ϊ skynet_ctx ����ռ�
	memset(s->slot, 0, s->slot_size * sizeof(struct skynet_context *));

	rwlock_init(&s->lock);
	// reserve 0 for system
	s->harbor = (uint32_t) (harbor & 0xff) << HANDLE_REMOTE_SHIFT;
	s->handle_index = 1;	// handle�����1��ʼ,0���� 0���ƴ�ԭ���Ǵ��㽫0�ŷ�����Ϊ�������ĵ�
	s->name_cap = 2;		// ����������ʼΪ2
	s->name_count = 0;
	s->name = malloc(s->name_cap * sizeof(struct handle_name));

	H = s;

	// Don't need to free H
}

