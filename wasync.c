#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#pragma comment(lib, "Ws2_32.lib")

static LPFN_CONNECTEX  ConnectEx = NULL;
static LPFN_DISCONNECTEX   DisconnectEx = NULL;

#define dbg_pr(_x_, ...) do {                                                                  \
                                fprintf(stderr, "[%s][%s:%d]", _x_, __FUNCTION__, __LINE__);   \
                                fprintf(stderr, __VA_ARGS__);                                  \
                            } while (0)

#define dbg_prnt(_x_, ...) do {                                                                 \
                                dbg_pr(_x_, __VA_ARGS__);                                       \
                                fprintf(stderr,"\n");                                           \
                            } while (0)

#define dbg(...) dbg_prnt("DBG", __VA_ARGS__)
#define err(...) dbg_prnt("ERR", __VA_ARGS__)

static void wsarecv_callback(DWORD error, DWORD bytes, WSAOVERLAPPED *overlapped, DWORD flags)
{
    if ( error )
    {
        err("Error in receiving : %d", error);
        return;
    }
    else  if ( ! bytes )
    {
        err("Connection closed while receiving");
        return;
    }
    else
    {
        dbg("Received bytes = %d", bytes);
    }
}

static void timercallback(void *arg, DWORD low, DWORD hi)
{
    HANDLE h = arg;
    dbg("Timercallback ++");
    DWORD res = CancelIoEx(arg, NULL);
    if(!res)
    {
        int error = WSAGetLastError();
        err("CancelIoEx failed: %d", error);
        return;
    }
    dbg("Timercallback --");
}

int main()
{
    WSADATA wsaData;
    int res = 0;
    SOCKET sock, csock;
    DWORD dwBytes;
    GUID GuidConnectEx = WSAID_CONNECTEX;
    GUID GuidGetSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
    GUID GuidDisconnectEx = WSAID_DISCONNECTEX;
    WSAOVERLAPPED Overlap;
    struct addrinfo hints;
    #define MAX_IO_PEND (10)
    WSAEVENT Handles[MAX_IO_PEND] = { 0 };
//    char host[] = "vsreen-p1";
    char host[] = "uksl-lvl2-vsreeniv-ub20-1";
    char port[] = "54321";
    struct addrinfo *ai_list = NULL;

    memset( &hints, 0, sizeof( hints ) );
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    memset(&Overlap, 0, sizeof(Overlap));
    Overlap.hEvent = WSACreateEvent();
    if (WSA_INVALID_EVENT == Overlap.hEvent)
    {
        printf("WSACreateEvent failed: %d\n", WSAGetLastError());
        return 1;
    }
    Handles[0] = Overlap.hEvent;

    res = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (res != 0) {
        printf("WSAStartup failed: %d\n", res);
        return 1;
    }

    sock = WSASocket( AF_UNSPEC, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED );
    if ( sock == INVALID_SOCKET )
    {
        int error = WSAGetLastError();
        err( "couldn't create socket, status %d",error);
        return -1;
    }
    dbg("socket created");

    WSAIoctl( sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
          &GuidConnectEx, sizeof( GuidConnectEx ),
          &ConnectEx, sizeof( ConnectEx ),
          &dwBytes, NULL, NULL );

    WSAIoctl( sock, SIO_GET_EXTENSION_FUNCTION_POINTER,
          &GuidDisconnectEx, sizeof( GuidDisconnectEx ),
          &DisconnectEx, sizeof( DisconnectEx ),
          &dwBytes, NULL, NULL );

    res = getaddrinfo( host, port, &hints, &ai_list );
    if (res)
    {
        int error = WSAGetLastError();
        err( "couldn't do getaddrinfo, status %d",error);
        return -1;
    }

    csock = WSASocket( ai_list->ai_family, ai_list->ai_socktype, ai_list->ai_protocol,
                                NULL, 0, WSA_FLAG_OVERLAPPED );
    if ( sock == INVALID_SOCKET )
    {
        int error = WSAGetLastError();
        err( "couldn't create socket, status %d",error);
        return -1;
    }
    unsigned long nonblocking = 1;
    if ( ioctlsocket( csock, FIONBIO, &nonblocking ) )
    {
        int error = WSAGetLastError();
        err( "couldn't set socket to nonblocking, status %d",error);
        return -1;
    }

    struct sockaddr_storage ws_addr;
    ZeroMemory( &ws_addr, sizeof( ws_addr ) );
    ws_addr.ss_family = ai_list->ai_family;

    if ( bind(csock, (struct sockaddr *)&ws_addr, sizeof(ws_addr)) )
    {
        int error = WSAGetLastError();
        err("couldn't bind, status %d",error);
        return -1;
    }

    dbg("Initiating Connectx");
    res = ConnectEx(csock, ai_list->ai_addr, (int)ai_list->ai_addrlen, NULL, 0, NULL, &Overlap);
    if (res != TRUE)
    {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING)
        {
            err("ConnectEx failed: %d", err);
            return 1;
        }
        else
        {
            dbg("ConnectEx returned ERROR_IO_PENDING");
        }
    }

    DWORD wait_rc = 0;
    dbg("Waiting for events. Expecting event once Acceptex is done\n");
    wait_rc = WSAWaitForMultipleEvents(1, Handles, FALSE, WSA_INFINITE, FALSE);
    if (wait_rc == WSA_WAIT_FAILED) {
        err("WSAWaitForMultipleEvents failed: %d\n", WSAGetLastError());
        return 1;
    }
    int triggered_handle_index = wait_rc - WSA_WAIT_EVENT_0;
    dbg("triggered_handle_index = %d", triggered_handle_index);

    DWORD bytes = 0, flags = 0;
    res = WSAGetOverlappedResult( csock, &Overlap, &bytes, FALSE, &flags );
    if (!res)
    {
        int error = WSAGetLastError();
        err("WSAGetOverlappedResult failed: %d", error);
        return 1;
    }
    dbg("Connect seems to be success");
    char true = 1;
    setsockopt( csock, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0 );
    setsockopt( csock, IPPROTO_TCP, TCP_NODELAY, (char *)&true, sizeof(true) );

    unsigned char buf[4096];
    WSABUF wsabuf;
    wsabuf.buf = buf;
    wsabuf.len = sizeof(buf);
    bytes = flags = 0;
    int err = 0;
    WSAOVERLAPPED overlapped;
    memset(&overlapped, 0 ,sizeof(overlapped));
    dbg("Requesting receive");
    res = WSARecv(csock, &wsabuf, 1, &bytes, &flags, &overlapped /*NULL*/, wsarecv_callback);
    if ( res && ((err = WSAGetLastError()) != WSA_IO_PENDING) )
    {
        err("wsarecv returned error : %d", err);
        return;
    }
    dbg("looks like wsarecv returned iopending or success, res = %d , err = %d", res, err);
    dbg("sleep for few seconds");
//    Sleep(5 * 1000);
#if 0
    res = DisconnectEx(csock, NULL, 0, 0);
    if (res != TRUE)
    {
        int err = WSAGetLastError();
        err("DisconnectEx failed, err = %d", err);
    }
    else
        dbg("Disconnect success");
#endif
    dbg("Creating Timer");

    HANDLE timer = CreateWaitableTimer(NULL, FALSE, NULL);
    if (!timer)
    {
        DWORD error = GetLastError();
        err("CreateWaitableTimer failed = %d", error);
        return -1;
    }
    dbg("Created Timer. Arming it");

    LARGE_INTEGER li;
    LONGLONG nanosec = (LONGLONG) 5 * 1000 * 1000 * 10;
    nanosec=-nanosec;
    li.LowPart = (DWORD)(nanosec & 0xFFFFFFFF);
    li.HighPart = (LONG)(nanosec >> 32);

    res = SetWaitableTimer( timer, &li, 0, timercallback, (void*)csock, FALSE );
    if(!res)
    {
        int error = GetLastError();
        err("SetWaitableTimer failed: %d", error);
        return 1;
    }

#if 0
    res = CancelIoEx(csock, NULL);
    if(!res)
    {
        int error = WSAGetLastError();
        err("CancelIoEx failed: %d", error);
        return 1;
    }
#endif
    Handles[0]= WSACreateEvent();
    dbg("WSAWaitForMultipleEvents ++");
    wait_rc = WSAWaitForMultipleEvents(1, Handles, FALSE, WSA_INFINITE, TRUE);
    if (wait_rc == WSA_WAIT_FAILED) {
        err("WSAWaitForMultipleEvents failed: %d\n", WSAGetLastError());
        return 1;
    }
    dbg("WSAWaitForMultipleEvents returned");

    Sleep(1000 * 10);
}
