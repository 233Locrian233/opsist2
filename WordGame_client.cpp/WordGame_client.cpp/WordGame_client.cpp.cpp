#include "Client.h"

inline void safeClose(HANDLE h) {
	if ((int64_t)h > 0) {
		CloseHandle(h);
	}
}

int _tmain(int argc, TCHAR* argv[])
{
	TCHAR playerName[ARRAY_SIZE + 1];
	bool threaded = false;

	if (!parseCommandLineArguments(argc, argv, (TCHAR*)playerName)) {
		return 0;
	}

	if (botMode) {
		warnServer = false;
	}

	// First initialize events, semaphores, pipe and shared memory
	if (initializeEventSemaphorePipeSharedMemory(playerName)) {

		// Then send login request to server
		if (loginToServer(playerName)) {

			// Initialize thread procedures
			if (!initializeThreads()) {

				/* One of the threads failed to start */
				SetEvent(quitHandle);	// Signal all threads to exit
			}
			else {

				threaded = true;
			}
		} else {
			warnServer = false;

		}
	}

	// Wait for all threads to complete
	if(threaded) {
		WaitForSingleObject(pipeThread, INFINITE);
		WaitForSingleObject(updateThread, INFINITE);
		TerminateThread(cliThread, INFINITE);
	}

	if (warnServer) {
		notifyLeave();	// let server know we're exiting
	}

	
	/*	 // Cleanup
	safeClose(pipeHandle);
	safeClose(semaphoreHandle);
	safeClose(updateHandle);	
	safeClose(serverHandle);
	safeClose(semaphoreHandle);
	safeClose(updateHandle);
	safeClose(quitHandle);
	safeClose(fileMappingHandle);
	*/

	return 0;
}