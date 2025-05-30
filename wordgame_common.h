#ifndef _wordgame_common_h_
#define _wordgame_common_h_

#define UNICODE
#define _UNICODE

#include <windows.h>
#include <tchar.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <map>
#include <sstream>
#include <functional>


#define DEBUG
#define ARRAY_SIZE 10
#define BUFFER_SIZE 256
#define MAX_PLAYERS 20
#define MAX_WORD_LENGTH 12
#define MAX_WORDS 128
#define _CRT_SECURE_NO_WARNINGS

typedef std::function<void(const TCHAR*)> cmd;

struct Login_Return_Type {
    int32_t flag;
    int32_t id;
};

enum MsgFlags {
    LOGIN,
    LOGOUT,
    GUESS,
    NAME_USED,
    SERVER_FULL,
    NO_EVENT,
    NO_PIPE,
    MVP,
    PLAYER_LOGIN,
    PLAYER_LOGOUT,
    SCORE,
    LIST
};

struct Packet {
    uint32_t code;
    int32_t id;
    TCHAR buffer[BUFFER_SIZE];
};

struct Dictionary {
    TCHAR words[MAX_WORDS][MAX_WORD_LENGTH + 1];
};

struct GameState {
    uint32_t t;         // Number of characters
    TCHAR array[BUFFER_SIZE];
};

const TCHAR* serverPipeName = TEXT("\\\\.\\pipe\\wordguess_pipe");
const TCHAR* sharedMemoryName = TEXT("Local\\shm");	// shared memory file name
const TCHAR* updatedSemaphoreName = TEXT("Local\\shm_semaphore");
const TCHAR* dictionaryName = TEXT("Local\\dictionary"); // path of word dictionary
LPTSTR botPath = _tcsdup(L"..\\..\\WordGame_client.cpp\\x64\\Release\\WordGame_client.cpp.exe");

/**
 * Parse command line input into command and arguments
 * Splits input into first word (command) and remaining text (arguments)
 *
 * @param firstWord Reference to store the command
 * @param secondWord Reference to store the arguments
 * @return Complete command line string
 */
std::wstring parseCmdline(std::wstring& firstWord, std::wstring& secondWord) {
    std::wstring cmdline;

    std::getline(std::wcin, cmdline);           // Read entire line
    std::wistringstream iss(cmdline);

    iss >> firstWord;                           // Extract first word (command)
    std::getline(iss, secondWord);              // Get remaining text (arguments)

    // Remove leading space from arguments if present
    if (!secondWord.empty() && secondWord[0] == L' ') {
        secondWord.erase(0, 1);
    }
    return cmdline;
}


#endif