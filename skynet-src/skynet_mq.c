#include "skynet.h"
#include "skynet_mq.h"
#include "skynet_handle.h"
#include "skynet_multicast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

// skynetʹ���˶�������  ��ȫ�ֵ� globe_mq ��ȡ mq ������

#define DEFAULT_QUEUE_SIZE 64       // Ĭ�϶��еĴ�С�� 64
#define MAX_GLOBAL_MQ 0x10000		// 64K,��������������64K�����global mq�������ֵҲ��64k
									// �����id�ռ���2^24��16M

//http://blog.codingnow.com/2012/08/skynet_bug.html

// 0 means mq is not in global mq.
// 1 means mq is in global mq , or the message is dispatching.
// 2 means message is dispatching with locked session set.
// 3 means mq is not in global mq, and locked session has been set.

#define MQ_IN_GLOBAL 1
#define MQ_DISPATCHING 2
#define MQ_LOCKED 3

// ��Ϣ����(ѭ������)���������̶�����������
// ��Ϣ���� mq �Ľṹ
struct message_queue {
	uint32_t handle;		// ��������handle
	int cap;				// ����
	int head;				// ��ͷ
	int tail;				// ��β
	int lock;				// ����ʵ�������� ���������ֵ����Ϊ1 �ͷ��������ֵ����Ϊ0
	int release;			// ��Ϣ�����ͷű�ǣ���Ҫ�ͷ�һ�������ʱ�� ������
							// ���������ͷŸ÷����Ӧ����Ϣ����(�п��ܹ����̻߳��ڲ���mq)������Ҫ����һ����� ����Ƿ�����ͷ�

	int lock_session;		// ��ĳ��session��Ӧ����Ϣ����
	int in_global;
	struct skynet_message *queue;	// ��Ϣ����
};

// ȫ�ֶ���(ѭ�����У���������)�������̶�64K �������е�ʵ��
// ������ ���е���Ϣ ���Ǵ����������ȡ��Ϣ����������
struct global_queue {
	uint32_t head;
	uint32_t tail;
	struct message_queue ** queue;	// ��Ϣ�����б�Ԥ��MAX_GLOBAL_MQ(64K)���ռ�
	bool * flag;	// ��queue��Ӧ��Ԥ��MAX_GLOBAL_MQ(64K)���ռ䣬���ڱ�ʶ��Ӧλ���Ƿ�����Ϣ����
					// ��ǰʵ�ֵ��������У���Ҫ�õ��ñ�� ������λ�� tail�Ѿ��ù��� �Ѿ���ȫ�������Ϣ���� ѹ��ȫ�ֵ���Ϣ������
};

static struct global_queue *Q = NULL; // ȫ�ֵ���Ϣ����  Q

// ����__sync_lock_test_and_setʵ�ֵ�������
#define LOCK(q) while (__sync_lock_test_and_set(&(q)->lock,1)) {}	//��q->lock����Ϊ1���������޸�ǰ��ֵ
#define UNLOCK(q) __sync_lock_release(&(q)->lock);		// ��q->lock��Ϊ0

#define GP(p) ((p) % MAX_GLOBAL_MQ) // �õ��ڶ����е�λ��

/*
	http://blog.codingnow.com/2012/10/bug_and_lockfree_queue.html
	Ϊ�˱�֤�ڽ����в�����ʱ��������ԭ�ӵ�������βָ��󣬻���Ҫ����һ�����λָʾ�����Ѿ�������д����У�
	���������ó������߳�ȡ����ȷ�����ݡ�
*/
static void 
skynet_globalmq_push(struct message_queue * queue) {
	struct global_queue *q= Q;

	uint32_t tail = GP(__sync_fetch_and_add(&q->tail,1));
	q->queue[tail] = queue; // �������Ϣ����ȫ�ֶ���
	__sync_synchronize();
	q->flag[tail] = true; // ������λ�� tail�Ѿ��ù��� �Ѿ���ȫ�������Ϣ���� ѹ��ȫ�ֵ���Ϣ������
}

/*
	һ��ʼ������ �������߳� æ��д�����߳� ���д���ǡ�
	��ԭ������д�����̵߳�������βָ���д����ɱ�Ǽ�ֻ��һ��������ָ�����æ������ȫû������ġ�
	�����Ҵ��ˣ��ټ�������£����������ݺ��٣������̷߳ǳ��ࣩ��Ҳ��������⡣
	�����Ľ�������ǣ��޸��˳����� api �����塣
	ԭ���������ǣ�
		������Ϊ�յ�ʱ�򣬷���һ�� NULL �������һ���Ӷ���ͷ��ȡ��һ����������
	�޸ĺ�������ǣ�
		���� NULL ��ʾȡ����ʧ�ܣ����ʧ�ܿ�������Ϊ���пգ�Ҳ�����������˾�����

	����������н���һ��ʹ�ã���ʹ�û����Ͽ����޸���� api ��������ȫ�����ģ�
	�޸ĺ���ȫ�������ǰ������⣬������ļ��˲������д���Ĵ��롣
*/
struct message_queue * 
skynet_globalmq_pop() {
	struct global_queue *q = Q;
	uint32_t head =  q->head;
	uint32_t head_ptr = GP(head);

	// ����Ϊ��
	if (head_ptr == GP(q->tail)) {
		return NULL;
	}

	// head����λ��û��mq
	if(!q->flag[head_ptr]) {
		return NULL;
	}

	__sync_synchronize();	// ͬ��ָ�� ��֤ǰ���ָ��ִ����ϣ��Ż�ִ�к����ָ��

	struct message_queue * mq = q->queue[head_ptr];

	// CASԭ���Բ��� ���q->head == head����q->head=head+1; �ƶ�ͷ��
	if (!__sync_bool_compare_and_swap(&q->head, head, head+1)) {
		return NULL;
	}

	q->flag[head_ptr] = false; // ��Ϣ�Ѿ���ȡ��

	return mq;
}

// ������Ϣ���У���ʼ���� DEFAULT_QUEUE_SIZE 64��
struct message_queue * 
skynet_mq_create(uint32_t handle) {
	struct message_queue *q = malloc(sizeof(*q));
	q->handle = handle;
	q->cap = DEFAULT_QUEUE_SIZE;
	q->head = 0;
	q->tail = 0;
	q->lock = 0; // ����
	q->in_global = MQ_IN_GLOBAL;
	q->release = 0;
	q->lock_session = 0;
	q->queue = malloc(sizeof(struct skynet_message) * q->cap);

	return q;
}

static void 
_release(struct message_queue *q) {
	free(q->queue);
	free(q);
}

uint32_t 
skynet_mq_handle(struct message_queue *q) {
	return q->handle;
}

int
skynet_mq_length(struct message_queue *q) {
	int head, tail,cap;

	LOCK(q)
	head = q->head;
	tail = q->tail;
	cap = q->cap;
	UNLOCK(q)
	
	if (head <= tail) {
		return tail - head; // ���� û��ѭ������
	}
	return tail + cap - head; // ѭ��������
}

int
skynet_mq_pop(struct message_queue *q, struct skynet_message *message) {
	int ret = 1;
	LOCK(q)

	// ��Ϣ���в�Ϊ��
	if (q->head != q->tail) {
		*message = q->queue[q->head];
		ret = 0;
		if ( ++ q->head >= q->cap) {
			q->head = 0;
		}
	}

	// û����Ϣ��������Ϣ����Ϊ�գ����ٽ���Ϣ����ѹ��ȫ�ֶ��� ��Ϣ����Ϊ�յľ��ǾͲ���ѹ�� globe_mq ��
	if (ret) {
		q->in_global = 0;
	}
	
	UNLOCK(q)

	return ret;
}

// ����mq 2���Ĵ�С����
static void
expand_queue(struct message_queue *q) {
	struct skynet_message *new_queue = malloc(sizeof(struct skynet_message) * q->cap * 2);
	int i;
	// ��ԭ������Ϣ�ᵽ�¶���
	for (i=0;i<q->cap;i++) {
		new_queue[i] = q->queue[(q->head + i) % q->cap];
	}
	q->head = 0;
	q->tail = q->cap;
	q->cap *= 2; // 2���Ĵ�С����
	
	free(q->queue); // �ͷ�ԭ���Ŀռ�
	q->queue = new_queue;
}

static void
_unlock(struct message_queue *q) {
	// this api use in push a unlock message, so the in_global flags must not be 0 , 
	// but the q is not exist in global queue.
	if (q->in_global == MQ_LOCKED) {
		skynet_globalmq_push(q);
		q->in_global = MQ_IN_GLOBAL;
	} else {
		assert(q->in_global == MQ_DISPATCHING);
	}
	q->lock_session = 0;
}

static void 
_pushhead(struct message_queue *q, struct skynet_message *message) {
	int head = q->head - 1;
	if (head < 0) {
		head = q->cap - 1;
	}
	// �������� ������� ����2��
	if (head == q->tail) {
		expand_queue(q);
		--q->tail;
		head = q->cap - 1;
	}

	q->queue[head] = *message;
	q->head = head;

	_unlock(q);
}

void 
skynet_mq_push(struct message_queue *q, struct skynet_message *message) {
	assert(message);
	LOCK(q)
	
	if (q->lock_session !=0 && message->session == q->lock_session) {
		_pushhead(q,message);
	} else {
		q->queue[q->tail] = *message;
		if (++ q->tail >= q->cap) {
			q->tail = 0;
		}

		if (q->head == q->tail) {
			expand_queue(q);
		}

		if (q->lock_session == 0) {
			if (q->in_global == 0) {
				q->in_global = MQ_IN_GLOBAL;
				skynet_globalmq_push(q);
			}
		}
	}
	
	UNLOCK(q)
}

void
skynet_mq_lock(struct message_queue *q, int session) {
	LOCK(q)
	assert(q->lock_session == 0);
	assert(q->in_global == MQ_IN_GLOBAL);
	q->in_global = MQ_DISPATCHING;
	q->lock_session = session;
	UNLOCK(q)
}

void
skynet_mq_unlock(struct message_queue *q) {
	LOCK(q)
	_unlock(q);
	UNLOCK(q)
}

// ��ʼ��ȫ����Ϣ���У������̶�64K
// �����������ֵ64K,���ȫ����Ϣ���������̶�64K,����ȫ����Ϣ����ʵ��Ϊ��������
void 
skynet_mq_init() {
	struct global_queue *q = malloc(sizeof(*q));
	memset(q,0,sizeof(*q));
	q->queue = malloc(MAX_GLOBAL_MQ * sizeof(struct message_queue *)); // 64����Ϣ����
	q->flag = malloc(MAX_GLOBAL_MQ * sizeof(bool)); // ��־���λ���Ƿ�����
	memset(q->flag, 0, sizeof(bool) * MAX_GLOBAL_MQ);
	Q=q;
}

void 
skynet_mq_force_push(struct message_queue * queue) {
	assert(queue->in_global);
	skynet_globalmq_push(queue);
}

// ��һ����Ϣ���в��뵽ȫ�ֶ���
void 
skynet_mq_pushglobal(struct message_queue *queue) {
	LOCK(queue)
	assert(queue->in_global);
	if (queue->in_global == MQ_DISPATCHING) {
		// lock message queue just now.
		queue->in_global = MQ_LOCKED;
	}
	if (queue->lock_session == 0) {
		skynet_globalmq_push(queue);
		queue->in_global = MQ_IN_GLOBAL;
	}
	UNLOCK(queue)
}

void 
skynet_mq_mark_release(struct message_queue *q) {
	LOCK(q)
	assert(q->release == 0);
	q->release = 1;
	if (q->in_global != MQ_IN_GLOBAL) {
		skynet_globalmq_push(q);
	}
	UNLOCK(q)
}

// ɾ����Ϣ����
static int
_drop_queue(struct message_queue *q) {
	// todo: send message back to message source
	struct skynet_message msg;
	int s = 0;
	while(!skynet_mq_pop(q, &msg)) {
		++s;
		int type = msg.sz >> HANDLE_REMOTE_SHIFT; // ���� 24 λ�õ��� 8 λ
		if (type == PTYPE_MULTICAST) {
			assert((msg.sz & HANDLE_MASK) == 0);
			skynet_multicast_dispatch((struct skynet_multicast_message *)msg.data, NULL, NULL);
		} else {
			free(msg.data);
		}
	}
	_release(q);
	return s;
}

int 
skynet_mq_release(struct message_queue *q) {
	int ret = 0;
	LOCK(q)
	
	if (q->release) {	// ���ͷű�ǣ���ɾ����Ϣ����q
		UNLOCK(q)
		ret = _drop_queue(q);
	} else {			// û�У�������ѹ��ȫ�ֶ���
		skynet_mq_force_push(q);
		UNLOCK(q)
	}
	
	return ret;
}
