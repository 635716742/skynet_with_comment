#ifndef SKYNET_IMP_H
#define SKYNET_IMP_H

// skynet ������
struct skynet_config {
	int thread; 			  // �߳���
	int harbor; 			  // harbor
	const char * logger; 	  // ��־����
	const char * module_path; // ģ�� �������·�� .so�ļ�·��
	const char * master;	  // master����
	const char * local;       // ����ip��port
	const char * start;       //
	const char * standalone;  // �Ƿ��ǵ�����
};

void skynet_start(struct skynet_config * config);

#endif
