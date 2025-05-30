#pragma once


#include "../../wordgame_common.h"
#include <iostream>
#include <windows.h>
#include <tchar.h>
#include <functional>
#include <sstream>
#include <map>


/*

	BEGIN GLOBAL_STATE
	
*/


TCHAR eventName[2 * ARRAY_SIZE + 2];
int32_t gameId = -1;	// game id, initially uninitialized

GameState* gameState;
Dictionary* dictionary;

/* threads */
HANDLE cliThread = INVALID_HANDLE_VALUE;
HANDLE updateThread = INVALID_HANDLE_VALUE;
HANDLE pipeThread = INVALID_HANDLE_VALUE;

/* pipes */
HANDLE pipeHandle = INVALID_HANDLE_VALUE;
HANDLE serverHandle = INVALID_HANDLE_VALUE;

/* shared memory access semaphore */
HANDLE semaphoreHandle = INVALID_HANDLE_VALUE;

/* my update signal */
HANDLE updateHandle = INVALID_HANDLE_VALUE;

/* global quit */
HANDLE quitHandle = INVALID_HANDLE_VALUE;

/* file mapping handle */
HANDLE fileMappingHandle = INVALID_HANDLE_VALUE;

/* dictionary mapping handle */
HANDLE dictMappingHandle = INVALID_HANDLE_VALUE;

/* Bot mode parameters */

bool botMode = false;

/* Flag for warning server when exiting */
bool warnServer = true;

/*

	END GLOBAL_STATE

*/

void displayGameState(const TCHAR* array, int t);

Packet transact(Packet& p)	// encapsulates a single packet transaction
{
	Packet res = { 0 };
	HANDLE pipeHandle = CreateFile(
		serverPipeName,
		GENERIC_READ | GENERIC_WRITE,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if (pipeHandle == INVALID_HANDLE_VALUE) {
#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(L"CreateFile %d\n", GetLastError());
#endif
		return res;
	}

	// Send packet and wait for acknowledgment
	if (!WriteFile(pipeHandle, &p, sizeof(Packet), NULL, NULL)) {
#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(L"WriteFile %d\n", GetLastError());
#endif
		return res;
	}

	if (!ReadFile(pipeHandle, &res, sizeof(Packet), NULL, NULL)) {
#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(L"ReadFile %d\n", GetLastError());
#endif
		return res;
	}


	CloseHandle(pipeHandle);
	return res;
}

void initCmds(std::map<std::wstring, cmd>& cmds)
{
	const HANDLE thisQuitHandle = quitHandle;

	cmds[std::wstring(L":pont")] = [](const TCHAR* args){
			Packet p = { 0 };
			p.code = SCORE;
			p.id = gameId;

			p = transact(p);
			std::wcout << L"Pontuação: " << p.id << L"\n";
		};
	
	cmds[std::wstring(L":lista")] = [](const TCHAR* args) {
		Packet p = { 0 };
		p.code = LIST;
		p.id = gameId;

		p = transact(p);
		std::wcout << L"Lista: " << p.buffer << L"\n";
		};

}

bool guessWord(const TCHAR* word)
{
	Packet packet = { 0 };
	packet.code = GUESS;
	CopyMemory(&packet.id, &gameId, 4);
	_tcscpy_s(packet.buffer, BUFFER_SIZE - 5, word);

	if ((serverHandle = CreateFile(
		serverPipeName,
		GENERIC_WRITE | GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	)) == INVALID_HANDLE_VALUE) {

#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(TEXT("CreateFile %d"), GetLastError());
#endif

		return false;
	}

	if (!WriteFile(serverHandle, &packet, sizeof(Packet), NULL, NULL)) {

#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(TEXT("WriteFile %d"), GetLastError());
#endif
		return false;
	}
	if (!ReadFile(serverHandle, &packet, sizeof(Packet), NULL, NULL)) {

#ifdef DEBUG
		std::cout << __func__ << " ";
		_tprintf(TEXT("WriteFile %d"), GetLastError());
#endif		
		return false;
	}

	CloseHandle(serverHandle);
	return true;
}

/* thread procedures */

void* botThreadProc(void* arg) {

	Packet p = { 0 };
	p.id = gameId;
	int32_t idx = 0;

	srand(time(NULL));	// reset prng seed
	
	while (true) {
		Sleep(2000);												// Sleep for X
		
		if (WaitForSingleObject(quitHandle, 0) == WAIT_OBJECT_0)	// if quit flag, leave
		{
			break;
		}

		idx = rand() % MAX_WORDS;									// randomly select index

		std::wcout << L"\n" << dictionary->words[idx] << L"\n";

		_tcscpy_s(p.buffer, BUFFER_SIZE, dictionary->words[idx]);	// send selected word
		
		transact(p);												// discard reply
	}

	return NULL;
}


void* cliThreadProc(void* arg)
{
	std::map<std::wstring, cmd> cmds;
	std::wstring input, first, second;
	initCmds(cmds);

	while (WaitForSingleObject(quitHandle, 0) != WAIT_OBJECT_0) {  // exit flag
		
		std::wcout << "\n > ";

		input = parseCmdline(first, second);
		
		if (input == L":sair") {

#ifdef DEBUG
			std::cout << "Thread " << __func__ << " exiting..." << std::endl;
#endif
			SetEvent(quitHandle);
			return NULL;
		}

		if (input[0] != L':') {
			if (!guessWord(input.c_str())) {
#ifdef DEBUG
				std::cout << "Thread " << __func__ << " exiting..." << std::endl;
#endif
				SetEvent(quitHandle);
				return NULL;
			}
		}
		else if (input == L":pont") {
			cmds[input](NULL);
		}
		else if (input == L":lista") {
			cmds[input](NULL);
		}
	}

	return NULL;
}

void* listenUpdateThreadProcBot(void* args)
{
	int32_t waitReturn;
	char buffer[BUFFER_SIZE];

	std::cout << "\nListening..." << std::endl;

	while (WaitForSingleObject(quitHandle, 0) != WAIT_OBJECT_0) {

		waitReturn = WaitForSingleObject(updateHandle, 100);

		if (waitReturn == WAIT_TIMEOUT) {
			continue;
		}

		if (waitReturn == WAIT_FAILED) {
			_tprintf(TEXT("WaitForSingle updateHandle %d\n"), GetLastError());
			SetEvent(quitHandle);
			return NULL;
		}

#ifdef DEBUG
		std::cout << "Waiting for semaphore..." << std::endl;
#endif

		// If we got to this point, then the update event was signalled by the server process

		waitReturn = WaitForSingleObject(semaphoreHandle, INFINITE);

		if (waitReturn == WAIT_FAILED) {
			_tprintf(TEXT("WaitForSingle semaphoreHandle %d\n"), GetLastError());
			SetEvent(quitHandle);
			return NULL;
		}


		ReleaseSemaphore(semaphoreHandle, 1, NULL);
	}


#ifdef DEBUG
	std::cout << "Thread " << __func__ << " exiting..." << std::endl;
#endif

	return NULL;
}

void* listenUpdateThreadProc(void* args)
{
	int32_t waitReturn;
	char buffer[BUFFER_SIZE];

	std::cout << "\nListening..." << std::endl;

	while (WaitForSingleObject(quitHandle, 0) != WAIT_OBJECT_0) {

		waitReturn = WaitForSingleObject(updateHandle, 100);

		if (waitReturn == WAIT_TIMEOUT) {
			continue;
		}

		if (waitReturn == WAIT_FAILED) {
			_tprintf(TEXT("WaitForSingle updateHandle %d\n"), GetLastError());
			SetEvent(quitHandle);
			return NULL;
		}

#ifdef DEBUG
		std::cout << "Waiting for semaphore..." << std::endl;
#endif

		// If we got to this point, then the update event was signalled by the server process

		waitReturn = WaitForSingleObject(semaphoreHandle, INFINITE);

		if (waitReturn == WAIT_FAILED) {
			_tprintf(TEXT("WaitForSingle semaphoreHandle %d\n"), GetLastError());
			SetEvent(quitHandle);
			return NULL;
		}

		displayGameState(gameState->array, gameState->t);

		ReleaseSemaphore(semaphoreHandle, 1, NULL);
	}


#ifdef DEBUG
	std::cout << "Thread " << __func__ << " exiting..." << std::endl;
#endif

	return NULL;
}

void* listenPipeThreadProc(void* args)
{
	OVERLAPPED overlapped = { 0 };
	Packet inputPacket = { 0 };
	bool result;

	if ((overlapped.hEvent = CreateEvent(
		NULL,
		FALSE,
		FALSE,
		NULL)) == NULL)
	{
		_tprintf(TEXT("CreateEvent %d"), GetLastError());
		SetEvent(quitHandle);
		return NULL;
	}

	while (WaitForSingleObject(quitHandle, 0) != WAIT_OBJECT_0) {

		result = ConnectNamedPipe(pipeHandle, &overlapped);

		if (!result) {
			int32_t errorCode = GetLastError();

			if (errorCode == ERROR_IO_PENDING)
			{
				int32_t waitResult = WaitForSingleObject(overlapped.hEvent, 1000);

				if (waitResult == WAIT_TIMEOUT) {
					// timeout reached
					continue;
				}
				// server connected within timeout

			}
			else if (errorCode == ERROR_PIPE_CONNECTED) {
				// Server connected between pipe creation and call
				SetEvent(overlapped.hEvent); // manually signal event
			}
			else {
				_tprintf(TEXT("ConnectNamedPipe %d"), errorCode);
				SetEvent(quitHandle);
				break;
			}
		}
	
		if ((ReadFile(pipeHandle, &inputPacket, sizeof(inputPacket), NULL, NULL)) == 0) {
			std::cout << __func__;
			_tprintf(TEXT(" ReadFile %d"), GetLastError());
			SetEvent(quitHandle);
			break;

		}

		_tprintf_s(L"Packet received: (%d, %d, %s)\n", inputPacket.code, inputPacket.id, inputPacket.buffer);


		FlushFileBuffers(pipeHandle);
		DisconnectNamedPipe(pipeHandle);

#ifdef DEBUG
		_tprintf((const TCHAR*)L"Message received: %d : %s\n", inputPacket.code, inputPacket.buffer);
#endif

		switch (inputPacket.code)
		{
			case PLAYER_LOGIN:
				/* a new player has joined */
				std::wcout << inputPacket.buffer << L" juntou-se ao jogo\n";
				break;

			case PLAYER_LOGOUT:
				/* someone left */
				std::wcout << inputPacket.buffer << L" saiu\n";
				break;

			case GUESS:
				/* someone guessed a word */
				std::wcout << inputPacket.buffer << " advinhou uma palavra.\n";
				break;

			case MVP:
				/* someone is on top of the leaderboard */
				std::wcout << inputPacket.buffer << L" passou á frente com " << inputPacket.id << L" pontuação";
				break;

			case LOGOUT:
				/* leave flag */
				std::wcout << "Foi kickado pelo servidor\n";

				SetEvent(quitHandle);

				CloseHandle(pipeHandle);
				CloseHandle(overlapped.hEvent);

				/* warnServer flag*/
				warnServer = false;

				return NULL;


			default:
				std::cout << "\nUnexpected packet flag received: " << inputPacket.code << std::endl;
				break;
			}
	}

#ifdef DEBUG
	std::cout << "Thread " << __func__ << " exiting..." << std::endl;
#endif

	CloseHandle(pipeHandle);
	CloseHandle(overlapped.hEvent);
	return NULL;
}

/* login and display */
bool loginToServer(const TCHAR* playerName) {
	Packet packet = { 0 };
	Login_Return_Type response;
	int32_t errorCode;
	packet.code = LOGIN;
	_tcscpy_s(packet.buffer, BUFFER_SIZE, playerName);

	if ((serverHandle = CreateFile(
		serverPipeName,
		GENERIC_WRITE | GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	)) == INVALID_HANDLE_VALUE) {
		_tprintf(TEXT("CreateFile %d"), GetLastError());
		return false;
	}

	if (!WriteFile(serverHandle, &packet, sizeof(Packet), NULL, NULL)) {
		_tprintf(TEXT("WriteFile %d"), GetLastError());
		SetEvent(quitHandle);
		return false;
	}
	
	if (!ReadFile(serverHandle, &response, sizeof(Login_Return_Type), NULL, NULL)) {
		std::cout << __func__;
		_tprintf(TEXT(" ReadFile %d"), GetLastError());
		SetEvent(quitHandle);
		return false;
	}

	CloseHandle(serverHandle);

	errorCode = response.flag;

	switch (errorCode) {
	case LOGIN:
		gameId = response.id;
#ifdef DEBUG
		std::cout << "gameID: " << gameId << "\n";
#endif
		return true;
	case SERVER_FULL:
		std::cout << "Server is full" << std::endl;
		SetEvent(quitHandle);
		return false;
	case NAME_USED:
		std::cout << "\nName already in use" << std::endl;
		SetEvent(quitHandle);
		return false;
	case NO_EVENT:
		std::cout << "\nNo event available" << std::endl;
		SetEvent(quitHandle);
		return false;
	case NO_PIPE:
		std::cout << "\nNo pipe available" << std::endl;
		SetEvent(quitHandle);
		return false;
	default:
		std::cout << "\nUnknown response flag: " << errorCode << std::endl;
		SetEvent(quitHandle);
		return false;
	}
}

void displayGameState(const TCHAR* array, int t)
{
	for (int i = 0; i < t; ++i) {
		if (array[i] == 0) {
			std::wcout << L" _ ";
		}
		else {
			std::wcout << L" " << array[i] << L" ";
		}
	}

	std::cout << "\n";
}

/* initialization procedures */

bool initializeThreads()
{
	if ((pipeThread = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)listenPipeThreadProc,
		NULL,
		0,
		NULL)) == NULL)
	{
		_tprintf(TEXT("CreateThread pipe %d"), GetLastError());
		return false;
	}


	/* If botmode, select botThreadPRoc as main word-guessing routine, else start cli routine */
	
	LPTHREAD_START_ROUTINE cli = (botMode) ? (LPTHREAD_START_ROUTINE)botThreadProc : (LPTHREAD_START_ROUTINE)cliThreadProc;

	if ((cliThread = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)cli,
		NULL,
		0,
		NULL)) == NULL)
	{
		_tprintf(TEXT("CreateThread cli %d"), GetLastError());
		SetEvent(quitHandle);
		return false;
	}
	
	
	if ((updateThread = CreateThread(
		NULL,
		0,
		(LPTHREAD_START_ROUTINE)listenUpdateThreadProc,
		NULL,
		0,
		NULL)) == NULL)
	{
		_tprintf(TEXT("CreateThread update %d"), GetLastError());
		SetEvent(quitHandle);
		return false;
	}

	return true;
}

bool initializeEventSemaphorePipeSharedMemory(const TCHAR* playerName)
{
	TCHAR thisPipeName[BUFFER_SIZE] = { 0 };
	_stprintf_s(thisPipeName, TEXT("%s%s"), TEXT("\\\\.\\pipe\\"), playerName);

	/* Initialize named pipe for server communication */
	if ((pipeHandle = CreateNamedPipe(
		thisPipeName,
		PIPE_ACCESS_DUPLEX | PIPE_TYPE_BYTE | FILE_FLAG_OVERLAPPED,
		PIPE_WAIT,
		PIPE_UNLIMITED_INSTANCES,
		2* BUFFER_SIZE * sizeof(TCHAR),
		2* BUFFER_SIZE * sizeof(TCHAR),
		0,
		NULL)) == INVALID_HANDLE_VALUE)
	{
		_tprintf(TEXT("CreateNamedPipe %d"), GetLastError());
		return false;
	}

	/* Initialize event name for updates */
	_stprintf_s(eventName, TEXT("%s%s%s"), TEXT("Local\\"), playerName, TEXT("_update"));

	/* Create update event for synchronization */
	if ((updateHandle = CreateEvent(
		NULL,
		FALSE,
		FALSE,
		eventName)) == NULL)
	{
		_tprintf(TEXT("CreateEvent %d"), GetLastError());
		return false;
	}

	/* Initialize quit handle for graceful shutdown */
	if ((quitHandle = CreateEvent(
		NULL,
		TRUE,
		FALSE,
		NULL)) == NULL)
	{
		_tprintf(TEXT("CreateEvent quit %d"), GetLastError());
		return false;
	}

	/* Open shared memory access semaphore */
	if ((semaphoreHandle = OpenSemaphore(
		SEMAPHORE_ALL_ACCESS,
		FALSE,
		updatedSemaphoreName
	)) == NULL) {
		_tprintf(TEXT("OpenSemaphore %d"), GetLastError());
		return false;
	}

	/* Open shared memory mapping */
	fileMappingHandle = OpenFileMapping(
		FILE_MAP_ALL_ACCESS,
		FALSE,
		sharedMemoryName
	);

	if (fileMappingHandle == NULL) {
		printf("OpenFileMapping failed (%d)\n", GetLastError());
		return false;
	}

	gameState = (GameState*)MapViewOfFile(
		fileMappingHandle,				// Handle to map object
		FILE_MAP_ALL_ACCESS,			// Read/write permission
		0, 0,							// Offset
		BUFFER_SIZE);

	if (gameState == NULL) {
		printf("MapViewOfFile failed (%d)\n", GetLastError());
		return false;
	}

	/* Open dictionary shared memory, if bot mode */
	
	if (botMode) {
		dictMappingHandle = OpenFileMapping(
			FILE_MAP_ALL_ACCESS,
			FALSE,
			dictionaryName
		);

		if (dictMappingHandle == NULL) {
			printf("OpenFileMapping dictionary failed (%d)\n", GetLastError());
			return false;
		}

		dictionary = (Dictionary*)MapViewOfFile(
			dictMappingHandle,								// Handle to map object
			FILE_MAP_ALL_ACCESS,							// Read/write permission
			0, 0,											// Offset
			0);												// Dictionary length in bytes

		if (dictionary == NULL) {
			printf("MapViewOfFile dictionary failed (%d)\n", GetLastError());
			return false;
		}
	}


	return true;
}

bool parseCommandLineArguments(int argc, TCHAR* argv[], TCHAR playerName[])
{
	// Check argument count: max 3 total (progname + username + optional -bot)
	if (argc > 3) {
		return false; // Too many arguments
	}

	bool hasName = false;

	for (int i = 1; i < argc; i++)
	{
		if (!_tcscmp(argv[i], L"-bot")) {
			if (botMode) {
				printf("Duplicate -bot flag\n");
				return false; // duplicate -bot flag
			}
			botMode = true;
		}
		else {
			if (hasName) {
				printf("Multiple username arguments\n");
				return false; // multiple username arguments
			}
			_tcscpy_s(playerName, ARRAY_SIZE + 1, argv[i]);
			hasName = true;
		}
	}

	if (!hasName) {
		_tcscpy_s(playerName, ARRAY_SIZE + 1, L"_test1");
	}

	return true;
}

inline void notifyLeave() {
	Packet packet = { 0 };
	packet.code = LOGOUT;
	packet.id = gameId;

	HANDLE serverHandle = CreateFile(
		serverPipeName,
		GENERIC_WRITE | GENERIC_READ,
		0,
		NULL,
		OPEN_EXISTING,
		0,
		NULL
	);

	if (serverHandle && serverHandle != INVALID_HANDLE_VALUE) {
		WriteFile(serverHandle, &packet, sizeof(Packet), NULL, NULL);
	}

}