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


#ifndef NETHASPMON_H_
#define NETHASPMON_H_

#define SVCNAME "NETHASPMON"
#define SVCNAMEDIS "Net HASP Monitor Exporter"
#define MAX_BUFF_SIZE 65536

typedef struct StrBuffer {
    char * data;
    char * pwrite;
    long size;
} StrBuffer;


extern StrBuffer *gdataResult;
extern StrBuffer *gdataDiscoResult;
extern HANDLE ghMutex;

void StrBufferClear(StrBuffer *buff);
int StrBufferLen(StrBuffer *buff);
StrBuffer *StrBufferCreate(const long size);
void StrBufferFree(StrBuffer *buff);
void StrBufferWrite(StrBuffer *buff, char * str, ...);

int http_start();
DWORD WINAPI http_run(LPVOID lpParam);
#endif /* NETHASPMON_H_ */
