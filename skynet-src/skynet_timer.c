#include "skynet_timer.h"
#include "skynet_mq.h"
#include "skynet_server.h"
#include "skynet_handle.h"
#include "skynet.h"

#include <time.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#if defined(__APPLE__)
#include <sys/time.h>
#endif

//http://blog.chinaunix.net/uid-11449555-id-2873961.html
//http://blog.csdn.net/gogofly_lee/article/details/2051669
//http://www.cnblogs.com/leaven/archive/2010/08/19/1803382.html

// skynet ��ʱ����ʵ��Ϊlinux�ں˵ı�׼����  ����Ϊ 0.01s ����Ϸһ����˵���� �߾��ȵĶ�ʱ���ܷ�CPU

typedef void (*timer_execute_func)(void *ud,void *arg);

// �����ں�����ĵġ�intervalֵ�ڣ�0��255��
// �ں��ڴ����Ƿ��е��ڶ�ʱ��ʱ������ֻ�Ӷ�ʱ����������tv1.vec��256���е�ĳ����ʱ�������ڽ���ɨ�衣
// ��2���������ں˲����ĵġ�intervalֵ�ڣ�0xff��0xffffffff��֮��Ķ�ʱ����
// ���ǵĵ��ڽ��ȳ̶�Ҳ����intervalֵ�Ĳ�ͬ����ͬ����ȻintervalֵԽС����ʱ�����ȳ̶�ҲԽ�ߡ�
// ����ڽ���������ɢ��ʱ������������֯ʱҲӦ������Դ���ͨ������ʱ����intervalֵԽС��
// �������Ķ�ʱ����������ɢ��Ҳ��Խ�ͣ�Ҳ�������еĸ���ʱ����expiresֵ���ԽС������intervalֵԽ��
// �������Ķ�ʱ����������ɢ��Ҳ��Խ��Ҳ�������еĸ���ʱ����expiresֵ���Խ�󣩡�
// ��ν����ɢ�Ķ�ʱ���������塱����ָ������ʱ����expiresֵ���Ի�����ͬ��һ����ʱ�����С�

// �ں˹涨��������Щ����������0x100��interval��0x3fff�Ķ�ʱ����
// ֻҪ���ʽ��interval>>8��������ֵͬ�Ķ�ʱ����������֯��ͬһ����ɢ��ʱ�������У�
// ����1��8��256Ϊһ��������λ����ˣ�Ϊ��֯������������0x100��interval��0x3fff�Ķ�ʱ����
// ����Ҫ2^6��64����ɢ��ʱ��������ͬ���أ�Ϊ�����������64����ɢ��ʱ������Ҳ����һ���γ����飬����Ϊ���ݽṹtimer_vec��һ���֡�
// ͬ�����0x4000��interval��0xfffff��0x100000��interval��0x3ffffff��0x4000000��interval��0xffffffff ���䣬
//ֻҪ���ʽ��interval>>8��6������interval>>8��6 +6������interval>>8��6+6+6����ֵ��ͬ�򱻷���ͬ�����ɢ��ʱ����
//������������ͬһ����ɢ��ʱ��������,��link_list t Ϊt[4],TIME_LEVEL_SHIFT Ϊ 6

#define TIME_NEAR_SHIFT 8
#define TIME_NEAR (1 << TIME_NEAR_SHIFT)   // 2^8 = 256
#define TIME_LEVEL_SHIFT 6
#define TIME_LEVEL (1 << TIME_LEVEL_SHIFT) // 64
#define TIME_NEAR_MASK (TIME_NEAR-1)       // 255
#define TIME_LEVEL_MASK (TIME_LEVEL-1)     // 63

struct timer_event {
	uint32_t handle;
	int session;
};

struct timer_node {
	struct timer_node *next;
	int expire;		// ��ʱ�δ���� ����ʱ���
};

struct link_list {
	struct timer_node head;
	struct timer_node *tail;
};

struct timer {
	struct link_list near[TIME_NEAR]; // ��ʱ�������� ����˲�ͬ�Ķ�ʱ������
	struct link_list t[4][TIME_LEVEL-1]; // 4���ݶ� 4����ͬ�Ķ�ʱ��
	int lock;               // ����ʵ��������
	int time;				// ��ǰ�Ѿ������ĵδ����
	uint32_t current;		// ��ǰʱ�䣬���ϵͳ����ʱ�䣨���ʱ�䣩
	uint32_t starttime;		// ��������ʱ�䣨����ʱ�䣩
};

static struct timer * TI = NULL;

// �����������ԭ�����һ���ڵ�ָ��
static inline struct timer_node *
link_clear(struct link_list *list)
{
	struct timer_node * ret = list->head.next;
	list->head.next = 0;
	list->tail = &(list->head);

	return ret;
}

// ��node��ӵ�����β��
static inline void
link(struct link_list *list,struct timer_node *node)
{
	list->tail->next = node;
	list->tail = node;
	node->next=0;
}

static void
add_node(struct timer *T,struct timer_node *node)
{
	int time=node->expire; // ��ʱ�ĵδ���
	int current_time=T->time;
	
	// ������ǵ�ǰʱ�� û�г�ʱ
	if ((time|TIME_NEAR_MASK)==(current_time|TIME_NEAR_MASK)) {
		link(&T->near[time&TIME_NEAR_MASK],node); // ���ڵ���ӵ���Ӧ��������
	}
	else {
		int i;
		int mask=TIME_NEAR << TIME_LEVEL_SHIFT;
		for (i=0;i<3;i++) {
			if ((time|(mask-1))==(current_time|(mask-1))) {
				break;
			}
			mask <<= TIME_LEVEL_SHIFT;
		}
		link(&T->t[i][((time>>(TIME_NEAR_SHIFT + i*TIME_LEVEL_SHIFT)) & TIME_LEVEL_MASK)-1],node);	
	}
}

static void
timer_add(struct timer *T,void *arg,size_t sz,int time)
{
	struct timer_node *node = (struct timer_node *)malloc(sizeof(*node)+sz);
	memcpy(node+1,arg,sz);

	while (__sync_lock_test_and_set(&T->lock,1)) {}; // lock

		node->expire=time+T->time;
		add_node(T,node);

	__sync_lock_release(&T->lock); // unlock
}

static void
timer_shift(struct timer *T) {
	int mask = TIME_NEAR;
	int time = (++T->time) >> TIME_NEAR_SHIFT;
	int i=0;
	
	while ((T->time & (mask-1))==0) {
		int idx=time & TIME_LEVEL_MASK;
		if (idx!=0) {
			--idx;
			struct timer_node *current = link_clear(&T->t[i][idx]);
			while (current) {
				struct timer_node *temp=current->next;
				add_node(T,current);
				current=temp;
			}
			break;				
		}
		mask <<= TIME_LEVEL_SHIFT;
		time >>= TIME_LEVEL_SHIFT;
		++i;
	}	
}

// �ӳ�ʱ�б���ȡ��ʱ����Ϣ���ַ�
static inline void
timer_execute(struct timer *T) {
	int idx = T->time & TIME_NEAR_MASK;
	
	while (T->near[idx].head.next) {
		struct timer_node *current = link_clear(&T->near[idx]);
		
		do {
			struct timer_event * event = (struct timer_event *)(current+1);
			struct skynet_message message;
			message.source = 0;
			message.session = event->session;
			message.data = NULL;
			message.sz = PTYPE_RESPONSE << HANDLE_REMOTE_SHIFT; // ����ƫ���� 24 λ

			skynet_context_push(event->handle, &message); // ����Ϣ���͵���Ӧ�� handle ȥ����
			
			struct timer_node * temp = current;
			current=current->next;
			free(temp);	
		} while (current);
	}
}

// ʱ��ÿ��һ���δ�ִ��һ�θú���
static void 
timer_update(struct timer *T)
{
	while (__sync_lock_test_and_set(&T->lock,1)) {}; // lock

	// try to dispatch timeout 0 (rare condition)
	timer_execute(T);

	// shift time first, and then dispatch timer message
	// ƫ�ƶ�ʱ�� ���ҷַ���ʱ����Ϣ ��ʱ��Ǩ�Ƶ����Ϸ�������λ��
	timer_shift(T);
	timer_execute(T);

	__sync_lock_release(&T->lock); // unlock
}

static struct timer *
timer_create_timer()
{
	struct timer *r=(struct timer *)malloc(sizeof(struct timer));
	memset(r,0,sizeof(*r));

	int i,j;

	for (i=0;i<TIME_NEAR;i++) {
		link_clear(&r->near[i]);
	}

	for (i=0;i<4;i++) {
		for (j=0;j<TIME_LEVEL-1;j++) {
			link_clear(&r->t[i][j]);
		}
	}

	r->lock = 0;
	r->current = 0;

	return r;
}

// ���붨ʱ����time�ĵ�λ��0.01�룬��time=300����ʾ3��
int
skynet_timeout(uint32_t handle, int time, int session) {
	if (time == 0) {
		struct skynet_message message;
		message.source = 0;
		message.session = session;
		message.data = NULL;
		message.sz = PTYPE_RESPONSE << HANDLE_REMOTE_SHIFT;

		if (skynet_context_push(handle, &message)) {
			return -1;
		}
	} else {
		struct timer_event event;
		event.handle = handle;
		event.session = session;
		timer_add(TI, &event, sizeof(event), time);
	}

	return session;
}

// ����ϵͳ���������ڵ�ʱ�䣬��λ�ǰٷ�֮һ�� 0.01s
static uint32_t
_gettime(void) {
	uint32_t t;
#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_MONOTONIC, &ti);
	t = (uint32_t)(ti.tv_sec & 0xffffff) * 100;
	t += ti.tv_nsec / 10000000;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	t = (uint32_t)(tv.tv_sec & 0xffffff) * 100; // &0xffffff ��Ҫ��Ϊ�˲���������ռ�   ʵ����������������ʱ��Ļ�����Ҳ�ǲ�Ӱ��� �ٳ�100 1s����100��0.01s
	t += tv.tv_usec / 10000; // ��λ�� 0.01 �� 10^-2s�� 10^-6s��ת��
#endif
	return t;
}

void
skynet_updatetime(void) {
	uint32_t ct = _gettime(); // 0.01
	if (ct != TI->current) {
		int diff = ct >= TI->current ? ct - TI->current : (0xffffff+1)*100 - TI->current+ct; // �õ�ʱ����
		TI->current = ct;
		int i;
		for (i=0;i<diff;i++) {
			timer_update(TI);
		}
	}
}

uint32_t
skynet_gettime_fixsec(void) {
	return TI->starttime;
}

uint32_t 
skynet_gettime(void) {
	return TI->current;
}

void 
skynet_timer_init(void) {
	TI = timer_create_timer(); // ���䶨ʱ���ṹ
	TI->current = _gettime();  // �õ��ĵ�ǰʱ�� ����ڿ�����

#if !defined(__APPLE__)
	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	uint32_t sec = (uint32_t)ti.tv_sec;
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	uint32_t sec = (uint32_t)tv.tv_sec;
#endif
	uint32_t mono = _gettime() / 100; // ��λ��0.01s ����������Ҫת�ɳɵ�λΪs

	TI->starttime = sec - mono; // ����ʱ��
}
