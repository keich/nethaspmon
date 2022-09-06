/*

MIT License

Copyright (c) 2022 Alexander Zazhigin mykeich@yandex.ru

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <windows.h>
#include <winsvc.h>
#include <tchar.h>
#include "nethaspmon.h"
#include <stdarg.h>

#define NORM 0
#define DEBUG 1
#define DISCOVERY 2
HANDLE hFile;
int metod = NORM;

StrBuffer *gdataResult;
StrBuffer *gdataDiscoResult;
HANDLE ghMutex;

SERVICE_STATUS gSvcStatus;
SERVICE_STATUS_HANDLE gSvcStatusHandle;
HANDLE ghSvcStopEvent = NULL;

VOID WINAPI SvcCtrlHandler(DWORD);
VOID WINAPI SvcMain(DWORD, LPTSTR *);

VOID ReportSvcStatus(DWORD, DWORD, DWORD);
VOID SvcInit(DWORD, LPTSTR *);

char *cfgname = NULL;

long __cdecl mightyfunc(const char *request, char *response, long *psize);

void StrBufferClear(StrBuffer *buff) {
    buff->data = buff->pwrite;
}

int StrBufferLen(StrBuffer *buff) {
    return buff->pwrite - buff->data;
}

int StrBufferWriteableSize(StrBuffer *buff) {
    return buff->size - (int) (buff->pwrite - buff->data);
}

StrBuffer *StrBufferCreate(const long size) {
    void * p = malloc(sizeof(StrBuffer) + size);
    if (p == NULL) {
        return NULL;
    }
    StrBuffer *buff = p;
    buff->size = size;
    buff->data = (p + sizeof(StrBuffer));
    buff->pwrite = buff->data;
    return buff;
}

void StrBufferFree(StrBuffer *buff) {
    free(buff);
}

void StrBufferCopy(StrBuffer *to, StrBuffer *from) {
    int size = StrBufferLen(from);
    if (to->size < size) {
        size = to->size - 1;
    }
    memcpy(to->data, from->data, size);
    to->data[size] = '\0';
    to->pwrite = to->data + size;
}

void StrBufferWrite(StrBuffer *buff, char * str, ...) {
    va_list args;
    va_start(args, str);
    int l = vsnprintf(buff->pwrite, StrBufferWriteableSize(buff), str, args);
    va_end(args);
    buff->pwrite += l;
}

#define MAX_LIC_SIZE 255

typedef struct module_t {
    int curr;
    int max;
} module_t;

void req(StrBuffer *buff, char *cmd) {
    mightyfunc(cmd, buff->data, &buff->size);
    if (metod == DEBUG) {
        printf(">> %s\n", cmd);
        printf("<< %s\n", buff->data);
    }
}

int setcfg(StrBuffer *buff, char *cfgname) {
    if (cfgname == NULL) {
        req(buff, "set config,filename=.\\NETHASP.INI");
    } else {
        char *prestr = "set config,filename=";
        size_t size = strlen(prestr) + strlen(cfgname) + 10;
        char str[size];
        strcpy(str, prestr);
        strcat(str, cfgname);
        req(buff, str);
    }
    return strncmp("OK", buff->data, 2);
}

int scan(StrBuffer *buff) {
    int count = 100;
    req(buff, "SCAN SERVERS");
    while (!strncmp("SCANNING", buff->data, 8) && count > 0) {
        sleep(1);
        count--;
        req(buff, "STATUS");
    }
    return strncmp("OK", buff->data, 2);
}

char *getp(char *str, char *param, int *size) {
    char *end = strstr(str, "\r\n");
    if (end == NULL) {
        end = str + strlen(str) - 1;
    }
    char *out = strstr(str, param);
    if (out == NULL || out > end) {
        *size = 0;
        return NULL;
    }
    out = out + strlen(param);
    char *scom = strstr(out, ",");
    if (scom != NULL && scom < end) {
        end = scom;
    }
    *size = end - out;
    return out;
}

void parse_mod(char *buffer, module_t *out) {
    char *curr, *max;
    int curr_size, max_size;
    curr = getp(buffer, "CURR=", &curr_size);
    out->curr = 0;
    if (curr != NULL) {
        out->curr = atoi(curr);
    }
    max = getp(buffer, "MAX=", &max_size);
    out->max = 0;
    if (max != NULL) {
        out->max = atoi(max);
    }
}

void getmod(StrBuffer *buff, char * id, int id_size, module_t *out) {
    const char *cmd = "GET MODULES,ID=";
    const int str1_size = strlen(cmd);
    char str1[str1_size + id_size + 1];
    strcpy(str1, cmd);
    strncpy(str1 + str1_size, id, id_size);
    str1[str1_size + id_size] = '\0';
    req(buff, str1);
    parse_mod(buff->data, out);
}

void jsonescape(char * str, int size) {
    for (int i = 0; i < size; i++) {
        switch (str[i]) {
        case '\b':
        case '\r':
        case '\n':
        case '\t':
        case '\"':
        case '\\':
            str[i] = ' ';
        }
    }
}

int parse_srv(char *buffer) {
    StrBuffer *buff = StrBufferCreate(MAX_BUFF_SIZE);
    char *id, *name, *prot, *ver, *os;
    int id_size, name_size, prot_size, ver_size, os_size;
    StrBuffer *dataResult = StrBufferCreate(MAX_BUFF_SIZE);
    StrBuffer *dataDiscoResult = StrBufferCreate(MAX_BUFF_SIZE);
    StrBufferWrite(dataResult, "{");
    StrBufferWrite(dataDiscoResult, "{ \"data\":[");
    int first = 1;
    while (buffer) {
        printf("debug5\n");
        id = getp(buffer, "ID=", &id_size);
        if (id == NULL) {
            buffer = strstr(buffer, "\r\n");
            if (buffer == NULL) {
                break;
            }
            buffer += 2;
            continue;
        }
        jsonescape(id, id_size);
        name = getp(buffer, "NAME=", &name_size);
        if (name == NULL) {
            buffer = strstr(buffer, "\r\n");
            if (buffer == NULL) {
                break;
            }
            buffer += 2;
            continue;
        }
        name += 1;
        name_size -= 2;
        jsonescape(name, name_size);
        prot = getp(buffer, "PROT=", &prot_size);
        if (prot == NULL) {
            buffer = strstr(buffer, "\r\n");
            if (buffer == NULL) {
                break;
            }
            buffer += 2;
            continue;
        }
        prot += 1;
        prot_size -= 2;
        jsonescape(prot, prot_size);
        ver = getp(buffer, "VER=", &ver_size);
        if (ver == NULL) {
            buffer = strstr(buffer, "\r\n");
            if (buffer == NULL) {
                break;
            }
            buffer += 2;
            continue;
        }
        ver += 1;
        ver_size -= 2;
        jsonescape(ver, ver_size);
        os = getp(buffer, "OS=", &os_size);
        if (os == NULL) {
            buffer = strstr(buffer, "\r\n");
            if (buffer == NULL) {
                break;
            }
            buffer += 2;
            continue;
        }
        os += 1;
        os_size -= 2;
        jsonescape(os, os_size);
        module_t mod = { .curr = 0, .max = 0 };
        getmod(buff, id, id_size, &mod);

        if (first) {
            first = 0;
        } else {
            StrBufferWrite(dataResult, ",");
            StrBufferWrite(dataDiscoResult, ",");
        }
        StrBufferWrite(dataResult, " \"%.*s\": {\"curr\": \"%d\",\"max\":\"%d\",\"name\": \"%.*s\",\"prot\": \"%.*s\",\"ver\": \"%.*s\",\"os\": \"%.*s\"}", name_size, name, mod.curr, mod.max, name_size, name, prot_size, prot, ver_size, ver, os_size, os);
        //StrBufferWrite(dataResult, " \"%.*s\": {\"curr\": \"%d\",\"max\":\"%d\",\"name\": \"%.*s\",\"prot\": \"%.*s\",\"ver\": \"%.*s\",\"os\": \"%.*s\"}", id_size, id, mod.curr, mod.max, name_size, name, prot_size, prot, ver_size, ver, os_size, os);
        StrBufferWrite(dataDiscoResult, "{\"{#SERVID}\": \"%.*s\",\"{#CURR}\": \"%d\",\"{#MAX}\":\"%d\",\"{#SERVNAME}\": \"%.*s\"}", id_size, id, mod.curr, mod.max, name_size, name);
        buffer = strstr(buffer, "\r\n");
        if (buffer == NULL) {
            break;
        }
        printf("debug13\n");
        buffer += 2;
    }
    StrBufferWrite(dataResult, "}");
    StrBufferWrite(dataDiscoResult, "] }");

    WaitForSingleObject(ghMutex, INFINITE);
    StrBufferCopy(gdataResult, dataResult);
    StrBufferCopy(gdataDiscoResult, dataDiscoResult);
    ReleaseMutex(ghMutex);

    StrBufferFree(buff);
    StrBufferFree(dataResult);
    StrBufferFree(dataDiscoResult);

    return 0;
}

int getserv(StrBuffer *buff) {
    req(buff, "GET SERVERINFO");
    if (!strncmp("ERROR", buff->data, 5)) {
        return 1;
    }
    parse_srv(buff->data);
    return 0;
}

void SvcReportEventInfo(LPTSTR szFunction) {
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];
    hEventSource = RegisterEventSource(NULL, SVCNAME);
    if (NULL != hEventSource) {
        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = szFunction;
        ReportEvent(hEventSource, EVENTLOG_SUCCESS, 0, 0, NULL, 2, 0, lpszStrings, NULL);               // no binary data
        DeregisterEventSource(hEventSource);
    }
}

//
// Purpose:
//   Entry point for the service
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None.
//
VOID WINAPI SvcMain(DWORD dwArgc, LPTSTR *lpszArgv) {
    gSvcStatusHandle = RegisterServiceCtrlHandler(SVCNAME, SvcCtrlHandler);
    gSvcStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gSvcStatus.dwServiceSpecificExitCode = 0;
    ReportSvcStatus(SERVICE_START_PENDING, NO_ERROR, 3000);
    SvcInit(dwArgc, lpszArgv);
}

//
// Purpose:
//   The service code
//
// Parameters:
//   dwArgc - Number of arguments in the lpszArgv array
//   lpszArgv - Array of strings. The first string is the name of
//     the service and subsequent strings are passed by the process
//     that called the StartService function to start the service.
//
// Return value:
//   None
//
VOID SvcInit(DWORD dwArgc, LPTSTR *lpszArgv) {
    gdataResult = StrBufferCreate(MAX_BUFF_SIZE);
    gdataDiscoResult = StrBufferCreate(MAX_BUFF_SIZE);
    ghSvcStopEvent = CreateEvent(
    NULL, TRUE, FALSE, NULL);
    if (ghSvcStopEvent == NULL) {
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
    if (http_start()) {
        ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
        return;
    }
    ReportSvcStatus(SERVICE_RUNNING, NO_ERROR, 0);
    DWORD dwWaitResult = 0;
    StrBuffer *buff = StrBufferCreate(MAX_BUFF_SIZE);
    int timecount = 600;
    int stop = 0;
    while (!stop) {
        if (timecount >= 5) {
            timecount = 0;
            if (setcfg(buff, cfgname)) {
                printf("setcfg failed\n");
                stop = 1;
            }
            if (scan(buff)) {
                printf("scan failed\n");
                stop = 1;
            }
            if (getserv(buff)) {
                printf("getserv failed\n");
                stop = 1;
            }
        }
        dwWaitResult = WaitForSingleObject(ghSvcStopEvent, 60000);
        timecount++;
        switch (dwWaitResult) {
        case WAIT_FAILED:
            printf("WaitForSingleObject fails (%lu)\n", GetLastError());
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            stop = 1;
            break;
        case WAIT_OBJECT_0:
            printf("Stop program signal\n");
            ReportSvcStatus(SERVICE_STOPPED, NO_ERROR, 0);
            stop = 1;
        }
    }
    StrBufferFree(buff);
    StrBufferFree(gdataResult);
    StrBufferFree(gdataDiscoResult);
}

//
// Purpose:
//   Sets the current service status and reports it to the SCM.
//
// Parameters:
//   dwCurrentState - The current state (see SERVICE_STATUS)
//   dwWin32ExitCode - The system error code
//   dwWaitHint - Estimated time for pending operation,
//     in milliseconds
//
// Return value:
//   None
//
VOID ReportSvcStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint) {
    static DWORD dwCheckPoint = 1;
    gSvcStatus.dwCurrentState = dwCurrentState;
    gSvcStatus.dwWin32ExitCode = dwWin32ExitCode;
    gSvcStatus.dwWaitHint = dwWaitHint;
    if (dwCurrentState == SERVICE_START_PENDING)
        gSvcStatus.dwControlsAccepted = 0;
    else
        gSvcStatus.dwControlsAccepted = SERVICE_ACCEPT_STOP;

    if ((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
        gSvcStatus.dwCheckPoint = 0;
    else
        gSvcStatus.dwCheckPoint = dwCheckPoint++;
    SetServiceStatus(gSvcStatusHandle, &gSvcStatus);
}

//
// Purpose:
//   Called by SCM whenever a control code is sent to the service
//   using the ControlService function.
//
// Parameters:
//   dwCtrl - control code
//
// Return value:
//   None
//
VOID WINAPI SvcCtrlHandler(DWORD dwCtrl) {
    switch (dwCtrl) {
    case SERVICE_CONTROL_STOP:
        ReportSvcStatus(SERVICE_STOP_PENDING, NO_ERROR, 0);
        SetEvent(ghSvcStopEvent);
        ReportSvcStatus(gSvcStatus.dwCurrentState, NO_ERROR, 0);
        return;
    case SERVICE_CONTROL_INTERROGATE:
        break;
    default:
        break;
    }
}

int SvcInstall() {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    int maxsize = MAX_PATH * 10;
    char szPath[maxsize];
    if (!GetModuleFileName(NULL, szPath, MAX_PATH)) {
        printf("Cannot install service (%lu)\n", GetLastError());
        return 1;
    }
    if (cfgname != NULL) {
        int size = strlen(szPath);
        snprintf(szPath + size, maxsize - size, " -c \"%s\"", cfgname);
    }
    printf("szPath %s\n", szPath);
    schSCManager = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        printf("OpenSCManager failed (%lu)\n", GetLastError());
        return 1;
    }
    schService = CreateService(schSCManager, SVCNAME, SVCNAMEDIS, SERVICE_ALL_ACCESS, SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, szPath, NULL, NULL, NULL, NULL, NULL);
    if (schService == NULL) {
        printf("CreateService failed (%lu)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return 1;
    } else
        printf("Service installed successfully\n");
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
    return 0;
}

void DoDeleteSvc() {
    SC_HANDLE schSCManager;
    SC_HANDLE schService;
    schSCManager = OpenSCManager( NULL, NULL, SC_MANAGER_ALL_ACCESS);
    if (NULL == schSCManager) {
        printf("OpenSCManager failed (%lu)\n", GetLastError());
        return;
    }
    schService = OpenService(schSCManager, SVCNAME, DELETE);
    if (schService == NULL) {
        printf("OpenService failed (%lu)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return;
    }
    if (!DeleteService(schService)) {
        printf("DeleteService failed (%lu)\n", GetLastError());
    } else
        printf("Service deleted successfully\n");
    CloseServiceHandle(schService);
    CloseServiceHandle(schSCManager);
}

int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-debug")) {
            metod = DEBUG;
            char cwd[PATH_MAX];
            if (getcwd(cwd, sizeof(cwd)) != NULL) {
                printf("Current working dir: %s\n", cwd);
            } else {
                perror("getcwd() error");
                return 1;
            }
        }
        if (!strcmp(argv[i], "-discovery")) {
            metod = DISCOVERY;
        }
        if (!strcmp(argv[i], "-c")) {
            i++;
            if (i < argc) {
                cfgname = argv[i];
            }
        }
        if (!strcmp(argv[i], "-i")) {
            SvcInstall();
            return 0;
        }
        if (!strcmp(argv[i], "-u")) {
            DoDeleteSvc();
            return 0;
        }
    }
    ghMutex = CreateMutex(
    NULL, FALSE, NULL);

    if (ghMutex == NULL) {
        printf("CreateMutex error: %lu\n", GetLastError());
        return 1;
    }

    SERVICE_TABLE_ENTRY DispatchTable[] = { { SVCNAME, (LPSERVICE_MAIN_FUNCTION) SvcMain }, { NULL, NULL } };
    StartServiceCtrlDispatcher(DispatchTable);

    gdataResult = StrBufferCreate(MAX_BUFF_SIZE);
    gdataDiscoResult = StrBufferCreate(MAX_BUFF_SIZE);
    StrBuffer *buff = StrBufferCreate(MAX_BUFF_SIZE);
    metod = DEBUG;

    if (setcfg(buff, cfgname)) {
        printf("setcfg failed\n");
    }
    if (scan(buff)) {
        printf("scan failed\n");
    }
    if (getserv(buff)) {
        printf("getserv failed\n");
    }
//    printf("DEBUG end\n");
//    req(buff, argv[1]);
    StrBufferFree(buff);
    StrBufferFree(gdataResult);
    StrBufferFree(gdataDiscoResult);
    CloseHandle(ghMutex);
}
