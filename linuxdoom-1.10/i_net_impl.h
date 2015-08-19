#ifndef __NET_IMPL__
#define __NET_IMPL__

#define IMPL_MAXNETNODES 8

void InitSystemNetLib (void);
int UDPsocket (void);
void BindToLocalPort(int s, int port);

int id_sendto(int socket, const void* data, int length, int flags, int address_index);
int id_recvfrom(int socket, void* data, int length, int flags, int* address_index);
void id_ioctl(int socket);

int GetLocalAddress (void);
void InitSendAddress (int address_index, int default_port, const char* address);

#endif//__NET_IMPL__
