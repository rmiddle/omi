/*
**==============================================================================
**
** Copyright (c) Microsoft Corporation. All rights reserved. See file LICENSE
** for license information.
**
**==============================================================================
*/

#ifndef _server_h
#define _server_h

#include <limits.h>
#include <unistd.h>
#include <protocol/protocol.h>
#include <pal/sleep.h>
#include <wsman/wsman.h>
#include <provreg/provreg.h>
#include <provmgr/provmgr.h>
#include <disp/disp.h>
#include <pal/strings.h>
#include <pal/dir.h>
#include <base/log.h>
#include <base/env.h>
#include <base/process.h>
#include <base/pidfile.h>
#include <base/paths.h>
#include <base/conf.h>
#include <base/user.h>
#include <base/omigetopt.h>
#include <base/multiplex.h>
#include <base/Strand.h>
#include <pal/format.h>
#include <pal/lock.h>

#if defined(CONFIG_POSIX)
# include <signal.h>
# include <sys/wait.h>
# include <pthread.h>
#endif

static const ZChar HELP[] = ZT("\
Usage: %s [OPTIONS]\n\
\n\
This program starts the server.\n\
\n\
OPTIONS:\n\
    -h, --help                  Print this help message.\n\
    -d                          Daemonize the server process (POSIX only).\n\
    -s                          Stop the server process (POSIX only).\n\
    -r                          Re-read configuration by the running server (POSIX only).\n\
    --reload-dispatcher         Re-read configuration by the running server (POSIX only), but don't unload providers.\n\
    --httpport PORT             HTTP protocol listener port.\n\
    --httpsport PORT            HTTPS protocol listener port.\n\
    --idletimeout TIMEOUT       Idle providers unload timeout (in seconds).\n\
    -v, --version               Print version information.\n\
    -l, --logstderr             Send log output to standard error.\n\
    --loglevel LEVEL            Set logging level to one of the following\n\
                                symbols/numbers: fatal/0, error/1, warning/2,\n\
                                info/3, debug/4, verbose/5 (default 2).\n\
    --httptrace                 Enable logging of HTTP traffic.\n\
    --timestamp                 Print timestamp server was built with.\n\
    --nonroot                   Run in non-root mode.\n\
    --service ACCT              Use ACCT as the service account.\n\
\n");

typedef struct _ServerData ServerData;

typedef enum _ServerTransportType
{
    SRV_PROTOCOL,
    SRV_WSMAN
}
ServerTransportType;

typedef struct _ServerCallbackData
{
    ServerData*         data;
    ServerTransportType type;
}
ServerCallbackData;

struct _ServerData
{
    Disp            disp;

    // 0 = socketfile, 1 = socketpair
    MuxIn           mux[2];
    ProtocolBase *protocol0;
    ProtocolSocketAndBase *protocol1;

    WSMAN**         wsman;
    int             wsman_size;
    Selector        selector;
    MI_Boolean      selectorInitialized;
    MI_Boolean      reloadDispFlag;
    MI_Boolean      terminated;

    /* pointers to self with different types - one per supported transport */
    ServerCallbackData  protocolData;
    ServerCallbackData  wsmanData;
};

typedef struct _Options
{
    MI_Boolean help;
#if !defined(CONFIG_FAVORSIZE)
    MI_Boolean trace;
#endif
    MI_Boolean httptrace;
    MI_Boolean terminateByNoop;
#if defined(CONFIG_POSIX)
    MI_Boolean daemonize;
    MI_Boolean stop;
    MI_Boolean reloadConfig;
    MI_Boolean reloadDispatcher;
#endif
    /* mostly for unittesting in non-root env */
    MI_Boolean ignoreAuthentication;
    MI_Boolean locations;
    MI_Boolean logstderr;
    unsigned short *httpport;
    int httpport_size;
    unsigned short *httpsport;
    int httpsport_size;
    char* sslCipherSuite;
    SSL_Options sslOptions;
    MI_Uint64 idletimeout;
    MI_Uint64 livetime;
    Log_Level logLevel;
    char *ntlmCredFile;
    MI_Boolean nonRoot;
    const char *serviceAccount;
    uid_t serviceAccountUID;
    gid_t serviceAccountGID;
    Sock socketpairPort;
}
Options;

typedef enum _ServerType { OMI_SERVER, OMI_ENGINE } ServerType;

static const char* arg0 = 0;


void PrintProviderMsg(_In_ Message* msg);
void GetCommandLineDestDirOption(int* argc_, const char* argv[]);
void GetCommandLineOptions(int* argc_, const char* argv[]);
void OpenLogFile();
void SetDefaults(Options *opts_ptr, ServerData *data_ptr, const char *executable, ServerType type);
void HandleSIGUSR1(int sig);
void GetConfigFileOptions();
void HandleSIGTERM(int sig);
void HandleSIGHUP(int sig);
void HandleSIGUSR1(int sig);
void HandleSIGCHLD(int sig);
void RequestCallback(_Inout_ InteractionOpenParams* interactionParams);
void FUNCTION_NEVER_RETURNS err(const ZChar* fmt, ...);
void FUNCTION_NEVER_RETURNS info_exit(const ZChar* fmt, ...);
void BinaryProtocolListenFile(const char *socketFile, MuxIn *mux, ProtocolBase **protocol);
void BinaryProtocolListenSock(Sock sock, MuxIn *mux, ProtocolSocketAndBase **protocol);
void WsmanProtocolListen();
void RunProtocol();
void InitializeNetwork();
void ServerCleanup(int pidfile);

#endif /* _server_h */
