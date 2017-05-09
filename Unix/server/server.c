/*
**==============================================================================
**
** Copyright (c) Microsoft Corporation. All rights reserved. See file LICENSE
** for license information.
**
**==============================================================================
*/

#include <stdlib.h>
#include <time.h>
#include <sock/sock.h>
#include <pal/dir.h>
#include <server/server.h>

static Options s_opts;
static ServerData s_data;
#define S_SOCKET_LENGTH 8
static char s_socket[S_SOCKET_LENGTH];

static int _StartEngine(int argc, char** argv, const char *sockFile)
{
    Sock s[2];
    char engineFile[PAL_MAX_PATH_SIZE];
    pid_t child;
    int fdLimit;
    int fd;
    int size;

    Strlcpy(engineFile, OMI_GetPath(ID_BINDIR), PAL_MAX_PATH_SIZE);
    Strlcat(engineFile, "/omiengine", PAL_MAX_PATH_SIZE);
    argv[0] = engineFile;

    BinaryProtocolListenFile(sockFile, &s_data.mux[0], &s_data.protocol0);
    
    if(socketpair(AF_UNIX, SOCK_STREAM, 0, s) != 0)
    {
        err(ZT("failed to create unix-domain socket pair"));
        return -1;
    }

    if (MI_RESULT_OK != Sock_SetBlocking(s[0], MI_FALSE) ||
        MI_RESULT_OK != Sock_SetBlocking(s[1], MI_FALSE))
    {
        trace_SetNonBlocking_Failed();
        return -1;
    }

    child = fork();
    if (child < 0)
    {
        err(PAL_T("fork failed"));
        return -1;  
    }

    if (child > 0)   // parent
    {
        Sock_Close(s[1]);
        BinaryProtocolListenSock(s[0], &s_data.mux[1], &s_data.protocol1);

        return 0;
    }

    // child code here

    Sock_Close(s[0]);

    if (SetUser(s_opts.serviceAccountUID, s_opts.serviceAccountGID) != 0)
    {
        err(PAL_T("failed to change uid/gid of engine"));
        return -1;
    }  

    /* Close all open file descriptors except provided socket
     (Some systems have UNLIMITED of 2^64; limit to something reasonable) */

    fdLimit = getdtablesize();
    if (fdLimit > 2500 || fdLimit < 0)
    {
        fdLimit = 2500;
    }

    /* ATTN: close first 3 also! Left for debugging only */
    for (fd = 3; fd < fdLimit; ++fd)
    {
        if (fd != s[1])
            close(fd);
    }

    argv[argc-1] = int64_to_a(s_socket, S_SOCKET_LENGTH, (long long)s[1], &size);

    execv(argv[0], argv);
    err(PAL_T("Launch failed"));
    exit(1);
}

static char** _DuplicateArgv(int argc, const char* argv[])
{
    int i;

    char **newArgv = (char**)malloc((argc+3)*sizeof(char*));

    // argv[0] will be filled in later
    if (argc > 1)
    {
        for (i = 1; i<argc; ++i)
        {
            newArgv[i] = (char*)argv[i];
        }
    }

    newArgv[argc] = "--socketpair";
    newArgv[argc+1] = NULL;  // to be filled later
    newArgv[argc+2] = NULL;

    return newArgv;
}

static int _GenerateRandomFileName(char *buffer, int length)
{
    const char letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    time_t t;
    int i;

    if (length > PAL_MAX_PATH_SIZE - 1)
        length = PAL_MAX_PATH_SIZE - 1;

    srand((unsigned) time(&t));
    for (i=0; i<length - 1; ++i)
    {
        buffer[i] = letters[rand() % sizeof(letters)];
    }
    buffer[length - 1] = '\0';

    return 0;
}

static int _CreateSockFile(char* buffer, int size)
{
#define SOCKET_FILE_NAME_LENGTH 10
    char sockDir[PAL_MAX_PATH_SIZE];
    char file[PAL_MAX_PATH_SIZE];
    char name[SOCKET_FILE_NAME_LENGTH];

    Strlcpy(sockDir, OMI_GetPath(ID_SYSCONFDIR), PAL_MAX_PATH_SIZE);
    Strlcat(sockDir, "/sockets", PAL_MAX_PATH_SIZE);

    Dir *dir = Dir_Open(sockDir);
    if (dir)
    {
        DirEnt *entry;
        while((entry = Dir_Read(dir)))
        {
            if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0)
                continue;

            Strlcpy(file, sockDir, PAL_MAX_PATH_SIZE);
            Strlcat(file, "/", PAL_MAX_PATH_SIZE);
            Strlcat(file, entry->name, PAL_MAX_PATH_SIZE);
            printf("Removing %s...\n", file);
            unlink(file);
        }
    }
    else
    {
        int r;
        r = Mkdir(sockDir, 0700);
        if (r != 0)
        {
            err(PAL_T("failed to create sockets directory: %T"), tcs(sockDir));
            return -1;
        }

        r = chown(sockDir, s_opts.serviceAccountUID, s_opts.serviceAccountGID);
        if (r != 0)
        {
            err(PAL_T("failed to chown sockets directory: %T"), tcs(sockDir));
            return -1;
        }
    }
        
    if ( _GenerateRandomFileName(name, SOCKET_FILE_NAME_LENGTH - 1) != 0)
    {
        err(PAL_T("Unable to generate socket file name"));
        return -1;
    }

    Strlcpy(buffer, sockDir, size);
    Strlcat(buffer, "/omi_", size);
    Strlcat(buffer, name, size);
    return 0;
}

static void _GetCommandLineNonRootOption(
    int* argc_,
    const char* argv[])
{
    int argc = *argc_;
    int i;
    s_opts.nonRoot = MI_FALSE;

    for (i = 1; i < argc; )
    {
        if (strncmp(argv[i], "--nonroot", 10) == 0)
        {
            s_opts.nonRoot = MI_TRUE;
            memmove((char*)&argv[i], (char*)&argv[i+1], 
                sizeof(char*) * (argc-i));

            argc -= 1;
        }
        else
            i++;
    }

    *argc_ = argc;
}

int servermain(int argc, const char* argv[])
{
#if defined(CONFIG_POSIX)
    int pidfile = -1;
#endif
    int engine_argc = 0;
    char **engine_argv = NULL;
    char socketFile[PAL_MAX_PATH_SIZE];

    arg0 = argv[0];

    SetDefaults(&s_opts, &s_data, arg0, OMI_SERVER);

    // Determine if we're running with non-root option
    _GetCommandLineNonRootOption(&argc, argv);
    if (s_opts.nonRoot == MI_TRUE)
    {
        engine_argc = argc + 2;
        engine_argv = _DuplicateArgv(argc, argv);
    }

    /* Get --destdir command-line option */
    GetCommandLineDestDirOption(&argc, argv);

    /* Extract configuration file options */
    GetConfigFileOptions();

    /* Extract command-line options a second time (to override) */
    GetCommandLineOptions(&argc, argv);

    /* Open the log file */
    OpenLogFile();

    /* Print help */
    if (s_opts.help)
    {
        Ftprintf(stderr, HELP, scs(arg0));
        exit(1);
    }

    /* Print locations of files and directories */
    if (s_opts.locations)
    {
        PrintPaths();
        Tprintf(ZT("\n"));
        exit(0);
    }

#if defined(CONFIG_POSIX)
    if (s_opts.stop || s_opts.reloadConfig)
    {
        if (PIDFile_IsRunning() != 0)
            info_exit(ZT("server is not running\n"));

        if (PIDFile_Signal(s_opts.stop ? SIGTERM : SIGHUP) != 0)
            err(ZT("failed to stop server\n"));

        if (s_opts.stop)
            Tprintf(ZT("%s: stopped server\n"), scs(arg0));
        else
            Tprintf(ZT("%s: refreshed server\n"), scs(arg0));

        exit(0);
    }
    if (s_opts.reloadDispatcher)
    {
        if (PIDFile_IsRunning() != 0)
            info_exit(ZT("server is not running\n"));

        if (PIDFile_Signal(SIGUSR1) != 0)
            err(ZT("failed to reload dispatcher on the server\n"));

        Tprintf(ZT("%s: server has reloaded its dispatcher\n"), scs(arg0));

        exit(0);        
    }
#endif

#if defined(CONFIG_POSIX)

    if (PIDFile_IsRunning() == 0)
        err(ZT("server is already running\n"));

    /* Verify that server is started as root */
    if (0 != IsRoot() && !s_opts.ignoreAuthentication)
    {
        err(ZT("expected to run as root"));
    }

    /* ATTN: unit-test support; should be removed/ifdefed later */
    if (s_opts.ignoreAuthentication)
    {
        IgnoreAuthCalls(1);
    }

    /* Watch for SIGTERM signals */
    if (0 != SetSignalHandler(SIGTERM, HandleSIGTERM) ||
        0 != SetSignalHandler(SIGHUP, HandleSIGHUP) ||
        0 != SetSignalHandler(SIGUSR1, HandleSIGUSR1))
        err(ZT("cannot set sighandler, errno %d"), errno);


    /* Watch for SIGCHLD signals */
    SetSignalHandler(SIGCHLD, HandleSIGCHLD);

#endif

    /* Change directory to 'rundir' */
    if (Chdir(OMI_GetPath(ID_RUNDIR)) != 0)
    {
        err(ZT("failed to change directory to: %s"), 
            scs(OMI_GetPath(ID_RUNDIR)));
    }

#if defined(CONFIG_POSIX)
    /* Daemonize */
    if (s_opts.daemonize && Process_Daemonize() != 0)
        err(ZT("failed to daemonize server process"));
#endif

#if defined(CONFIG_POSIX)

    /* Create PID file */
    if ((pidfile = PIDFile_OpenWrite()) == -1)
    {
        fprintf(stderr, "Could not create pid file %s\n", OMI_GetPath(ID_PIDFILE));
        trace_CreatePIDFileFailed( scs(OMI_GetPath(ID_PIDFILE)) );

        // Need to let the world know. We may not have a functioning log system at this point
        // or know to look

        fprintf(stderr, "Cannot create PID file. omi server exiting\n");
        exit(1);
    }

#endif

    /* If ntlm cred file is in use, check permissions and set NTLM_USER_FILE env variable */

    char *ntlm_user_file = getenv("NTLM_USER_FILE");
    if (ntlm_user_file)
    {
        /* We do NOT accept the NTLM_USER_FILE environement variable for the server */
        trace_NtlmEnvIgnored(ntlm_user_file);
        unsetenv("NTLM_USER_FILE");
    }

    if (s_opts.ntlmCredFile && !s_opts.ignoreAuthentication)
    {
       if (!ValidateNtlmCredsFile(s_opts.ntlmCredFile))
       {
           trace_NtlmCredFileInvalid(s_opts.ntlmCredFile);
       }
    }

    if (s_opts.nonRoot == MI_TRUE)
    {
        int r;

        r = _CreateSockFile(socketFile, PAL_MAX_PATH_SIZE);
        if (r != 0)
        {
            err(ZT("failed to create socket file"));
            exit(1);
        }

        InitializeNetwork();

        r = _StartEngine(engine_argc, engine_argv, socketFile);
        if (r != 0)
        {
            err(ZT("failed to start omi engine"));
            exit(1);
        }

        free(engine_argv);
    }

    while (!s_data.terminated)
    {
        if (s_opts.nonRoot != MI_TRUE)
        {
            InitializeNetwork();

            WsmanProtocolListen();

            BinaryProtocolListenFile(OMI_GetPath(ID_SOCKETFILE), &s_data.mux[0], &s_data.protocol0);
        }

        RunProtocol();
    }

    ServerCleanup(pidfile);

    return 0;
}
