#include "Server.h"

void safeClose(HANDLE h) {
    if(h != INVALID_HANDLE_VALUE){
        CloseHandle(h);
    }
}

int _tmain(int argc, TCHAR* argv[])
{
    bool threaded = false;    
    int ritmo = dwordFromRegistryKey(L"SOFTWARE\\TrabSO2", L"RITMO");
    int maxletras = dwordFromRegistryKey(L"SOFTWARE\\TrabSO2", L"MAXLETRAS");

    if (maxletras > 0) {
        LETTERS = (maxletras < 6) ? 6 : (maxletras > 12 ? 12 : maxletras);  // 6 <= LETTERS <= 12
    }   // else use default value

    if (ritmo > 0) {
        INTERVAL = ritmo * 1000;   // 1000 <= INTERVAL
    }   // else use default value

    if (initShmEventsSemaphore()) {

        if (initDictionary()) {
            
            if (initThreads()) {
                threaded = true;
            }
        }
        
    }

    if (threaded) {
        WaitForSingleObject(game_thread, INFINITE);
        WaitForSingleObject(listen_thread, INFINITE);
        TerminateThread(cli_thread, 0);
    }
    
    data.warnLeave();   // inform all clients of server shutdown

    UnmapViewOfFile(fm);
    CloseHandle(fm);
    UnmapViewOfFile(dictionary_handle);
    CloseHandle(dictionary_handle);
    CloseHandle(file_handle);
    CloseHandle(game_thread);
    CloseHandle(cli_thread);
    CloseHandle(listen_thread);
    CloseHandle(clear_handle);
    CloseHandle(semaphore_handle);
    CloseHandle(quit_handle);
    return 0;
}
