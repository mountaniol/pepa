#ifndef _PEPA_IP_STRUCT_H_
#define _PEPA_IP_STRUCT_H_

struct ip_struct {
	char ip[IP_LEN];
	int  port;
};
typedef  struct ip_struct ip_port_t;

#endif /* _PEPA_IP_STRUCT_H_ */
