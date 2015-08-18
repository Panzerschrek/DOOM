#include <winsock2.h>

void I_Error (char *error, ...);

struct sockaddr_in	sendaddress[8];

void InitNetLib()
{
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);
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
    int			trueval;
	
    memset (&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = port;
			
    v = bind (s, (void *)&address, sizeof(address));
    if (v == -1)
	I_Error ("BindToPort: bind: %s", strerror(errno));

    ioctlsocket (s, FIONBIO, &trueval);
}

int id_sendto(int socket, const void* data, int length, int flags, int address_index)
{
    return sendto(socket, data, length, flags, &sendaddress[address_index], sizeof(struct sockaddr_in));
}

int id_recvfrom(int socket, void* data, int length, int flags, int* address_index)
{
    struct sockaddr_in	fromaddress;
    int			fromlen;
    int			i;
    int			c;

    fromlen = sizeof(fromaddress);
    c = recvfrom(socket, data, length, flags, &fromaddress, &fromlen);

    for( i = 0; i < 8; i++ )
	if (fromaddress.sin_addr.s_addr== sendaddress[i].sin_addr.s_addr)
	{
	    *address_index = i;
	    break;
	}
    return c;
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

int InitSendAddress(int address_index, int port, const char* address)
{
    struct hostent*	hostentry;	// host information entry

    sendaddress[address_index].sin_family = AF_INET;
    sendaddress[address_index].sin_port = htons(port);

    if (address[0] == '.')
    {
	sendaddress[address_index].sin_addr.s_addr 
		= inet_addr (address+1);
    }
    else
    {
	hostentry = gethostbyname (address);
	if (!hostentry)
	    hostentry = inet_addr (address);
	if (!hostentry)
	    I_Error ("gethostbyname: couldn't find %s", address);
	sendaddress[address_index].sin_addr.s_addr 
	    = *(int *)hostentry->h_addr_list[0];
    }
}