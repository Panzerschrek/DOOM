#ifdef PANZER_DOOM_WIDNOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/ioctl.h>
#endif

#include "i_net_impl.h"

void I_Error (char *error, ...);



struct sockaddr_in	sendaddress[IMPL_MAXNETNODES];

void InitSystemNetLib (void)
{
#ifdef PANZER_DOOM_WIDNOWS
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif
}

//
// UDPsocket
//
int UDPsocket (void)
{
    int	s;

    // allocate a socket
    s = socket (PF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s<0)
	I_Error ("can't create socket: %s",strerror(errno));

    return s;
}

//
// BindToLocalPort
//
void
BindToLocalPort
( int	s,
  int	port )
{
    int			v;
    struct sockaddr_in	address;

    memset (&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = port;

    v = bind (s, (void *)&address, sizeof(address));
    if (v == -1)
	I_Error ("BindToPort: bind: %s", strerror(errno));
}

int id_sendto(int socket, const void* data, int length, int flags, int address_index)
{
    return sendto(socket, data, length, flags, (struct sockaddr*) &sendaddress[address_index], sizeof(struct sockaddr_in));
}

int id_recvfrom(int socket, void* data, int length, int flags, int* address_index)
{
    struct sockaddr_in	fromaddress;
    int			fromlen;
    int			i;
    int			c;

    fromlen = sizeof(fromaddress);
    c = recvfrom(socket, data, length, flags, (struct sockaddr*) &fromaddress, &fromlen);

    for( i = 0; i < IMPL_MAXNETNODES; i++ )
	if (fromaddress.sin_addr.s_addr== sendaddress[i].sin_addr.s_addr)
	{
	    *address_index = i;
	    break;
	}
    return c;
}

void id_ioctl(int socket)
{
    unsigned long	trueval = 1;

#ifdef PANZER_DOOM_WIDNOWS
    ioctlsocket (socket, FIONBIO, &trueval);
#else
    ioctl (socket, FIONBIO, &trueval);
#endif
}

int GetLocalAddress (void)
{
    char		hostname[1024];
    struct hostent*	hostentry;	// host information entry
    int			v;

    // get local address
    v = gethostname (hostname, sizeof(hostname));
    if (v == -1)
	I_Error ("GetLocalAddress : gethostname: errno %d",errno);

    hostentry = gethostbyname (hostname);
    if (!hostentry)
	I_Error ("GetLocalAddress : gethostbyname: couldn't get local host");

    return *(int *)hostentry->h_addr_list[0];
}

void InitSendAddress(int address_index, int port, const char* address)
{
    struct hostent*	hostentry;	// host information entry
    int			i;
    char		address_c[64];

    strcpy(address_c, address);

    i = 0;
    while (address_c[i] != 0)
    {
       if (address_c[i] == ':')
       {
	    if (address_c[i+1] == 0)
		I_Error("error, expected port, after ':' in %s", address_c);
	    port = atoi(address_c + i + 1);
	    address_c[i] = 0;
	    break;
       }
       i++;
    }

    sendaddress[address_index].sin_family = AF_INET;
    sendaddress[address_index].sin_port = htons(port);

    if (address[0] == '.')
    {
	sendaddress[address_index].sin_addr.s_addr
		= inet_addr (address_c+1);
    }
    else
    {
	hostentry = gethostbyname (address_c);
	if (!hostentry)
	    I_Error ("gethostbyname: couldn't find %s", address_c);
	sendaddress[address_index].sin_addr.s_addr
	    = *(int *)hostentry->h_addr_list[0];
    }
}
