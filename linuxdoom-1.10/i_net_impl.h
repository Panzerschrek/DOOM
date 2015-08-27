// Copyright (C) 2015 by Artöm "Panzerschrek" Kunç.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.


#ifndef __NET_IMPL__
#define __NET_IMPL__

#define IMPL_MAXNETNODES 8

void InitSystemNetLib (void);
int UDPsocket (void);
void BindToLocalPort(int s, int port);

int SendTo(int socket, const void* data, int length, int flags, int address_index);
int RecvFrom(int socket, void* data, int length, int flags, int* address_index);
void SocketIoctl(int socket);

int GetLocalAddress (void);
void InitSendAddress (int address_index, int default_port, const char* address);

#endif//__NET_IMPL__
