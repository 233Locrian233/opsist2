#include "..\\..\\wordgame_common.h"

/* project specific */
#include "GameData.h"

/*

    BEGIN GLOBAL_STATE

*/

/* Synchronization objects for thread coordination and shared memory access */
HANDLE semaphore_handle;    // Semaphore to control access to shared memory (allows MAX_PLAYERS + 2)
HANDLE data_handle;         // Mutex to protect GameData structure from concurrent access
HANDLE clear_handle;        // Event that signals when the game array should be cleared
HANDLE quit_handle;         // Global quit flag event for graceful shutdown
HANDLE fm;                  // File mapping handle for game state shared memory
HANDLE dictionary_handle;   // File mapping handle for dictionary shared memory
HANDLE file_handle;         // Handle for 'dictionary.txt'

/* Thread handles for the three main server threads */
HANDLE game_thread;         // Main game logic thread (generates letters)
HANDLE listen_thread;       // Client connection listener thread
HANDLE cli_thread;          // Command line interface thread for admin commands

/* Core game state and data */
GameData data;              // Player management and game data
GameState* state;           // Shared memory structure containing game state
Dictionary* dictionary;     // Shared memory structure containing word dictionary
uint32_t INTERVAL = 2000;   // Time interval between letter generation (milliseconds)
uint32_t LETTERS = 10;      // Number of letters of wordgame
std::map<std::wstring, bool> word_map;  // for quick dictionary verification

/*

    END GLOBAL_STATE

*/

/* Forward declarations for thread functions and utilities */
void* game(void* param);
void* _listen(void* param);
void* cli(void* param);
bool word_match(const TCHAR* input, const TCHAR* array);

/* init procedures */

/**
 * Initialize shared memory, events, and synchronization objects
 * Creates:
 * - File mapping for shared GameState structure
 * - File mapping for shared Dictionary structure
 * - Clear event (manual reset) for array clearing signal
 * - Quit event (manual reset) for shutdown coordination
 * - Semaphore for shared memory access control (MAX_PLAYERS + 2 capacity)
 * - Mutex for GameData protection
 *
 * @return true if all objects created successfully, false otherwise
 */
bool initShmEventsSemaphore()
{
    // Get system allocation granularity for proper memory alignment
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    DWORD granularity = sysInfo.dwAllocationGranularity;
    DWORD alignedOffset = (BUFFER_SIZE / granularity) * granularity;

    // Create file mapping for shared memory (BUFFER_SIZE bytes for GameState)
    fm = CreateFileMapping(
        INVALID_HANDLE_VALUE,	// create new file (not backed by disk file)
        NULL,                   // default security attributes
        PAGE_READWRITE,	        // read/write access
        0,                      // maximum object size (high-order DWORD)
        BUFFER_SIZE,            // maximum object size (low-order DWORD)
        sharedMemoryName        // name for the mapping object
    );

    if (fm == INVALID_HANDLE_VALUE || fm == NULL) {
        std::cout << "CreateFileMapping " << GetLastError() << std::endl;
        return false;
    }

    // Map the shared memory into this process's address space
    state = (GameState*)MapViewOfFile(
        fm,
        FILE_MAP_ALL_ACCESS,	// read/write access for other processes
        0,                      // file offset (high-order DWORD)
        alignedOffset,          // file offset (low-order DWORD)
        0                       // number of bytes to map (0 = entire file)
    );

    if (state == NULL) {
        std::cout << "MapViewOfFile " << GetLastError() << std::endl;
        return false;
    }

    // Realign offset
    alignedOffset = ((MAX_WORDS * MAX_WORD_LENGTH * sizeof(TCHAR)) / granularity) * granularity;

    // Create file mapping for dictionary shared memory
    dictionary_handle = CreateFileMapping(
        NULL,	                                                        // create new file mapping
        NULL,                                                           // default security attributes
        PAGE_READWRITE,	                                                // read/write access
        0,                                                              // maximum object size (high-order DWORD)
        MAX_WORDS * (MAX_WORD_LENGTH + 1) * sizeof(TCHAR),                // maximum object size (low-order DWORD) (0 = file size)
        dictionaryName                                                  // name for the mapping object
    );

    if (dictionary_handle == INVALID_HANDLE_VALUE || dictionary_handle == NULL) {
        std::cout << "CreateFileMapping " << GetLastError() << std::endl;
        return false;
    }

    // Map the shared memory into this process's address space
    dictionary = (Dictionary*)MapViewOfFile(
        dictionary_handle,
        FILE_MAP_ALL_ACCESS,	                                // read/write access for other processes
        0,                                                      // file offset (high-order DWORD)
        alignedOffset,                                          // file offset (low-order DWORD)
        MAX_WORDS * (MAX_WORD_LENGTH  + 1) * sizeof(TCHAR)      // number of bytes to map (0 = entire file)
    );

    if (dictionary == NULL) {
        std::cout << "MapViewOfFile " << GetLastError() << std::endl;
        return false;
    }


    // Create manual reset event for array clearing signal
    if ((clear_handle = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateEvent %d"), GetLastError());
        return false;
    }

    // Create manual reset event for global quit flag
    if ((quit_handle = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateEvent %d"), GetLastError());
        return false;
    }

    // Create semaphore for shared memory access control
    // Initial and maximum count = MAX_PLAYERS + 2 (allows game thread + listen thread + all players)
    if ((semaphore_handle = CreateSemaphore(NULL, MAX_PLAYERS + 2, MAX_PLAYERS + 2, updatedSemaphoreName)) == NULL)
    {
        _tprintf(TEXT("CreateSemaphore %d"), GetLastError());
        return false;
    }

    // Create mutex for protecting GameData structure
    if ((data_handle = CreateMutex(NULL, FALSE, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateMutex %d"), GetLastError());
        return false;
    }
}

/* Initialize dictionary contents */

bool initDictionary() {
    int i, j;
    FILE* inputFile;
    TCHAR buffer[BUFFER_SIZE];

    // Initialize dictionary array
    for (i = 0; i < MAX_WORDS; i++) {
        for (j = 0; j <= MAX_WORD_LENGTH; j++) {
            dictionary->words[i][j] = _T('\0');
        }
    }

    _tprintf(_T("Loading dictionary from text file...\n"));

    // Open dictionary file as regular text file
    if (_tfopen_s(&inputFile, _T("words.txt"), _T("r")) != 0) {
        _tprintf(_T("Error: Could not open 'words.txt' file\n"));
        
        return false;
    }

    // Read words line by line
    i = 0;
    while (i < MAX_WORDS && _fgetts(buffer, BUFFER_SIZE, inputFile) != NULL) {
        // Remove newline character if present
        int len = _tcslen(buffer);
        if (len > 0 && (buffer[len - 1] == _T('\n') || buffer[len - 1] == _T('\r'))) {
            buffer[len - 1] = _T('\0');
            len--;
        }
        if (len > 1 && buffer[len - 1] == _T('\r')) {
            buffer[len - 1] = _T('\0');
            len--;
        }

        // Skip empty lines
        if (len == 0) {
            continue;
        }

        // Truncate word if longer than MAX_WORD_LENGTH
        if (len > MAX_WORD_LENGTH) {
            buffer[MAX_WORD_LENGTH] = _T('\0');
            len = MAX_WORD_LENGTH;
        }

        // Copy word to shared memory
        _tcscpy_s(dictionary->words[i], MAX_WORD_LENGTH + 1, buffer);

#ifdef DEBUG
        _tprintf(_T("Loaded word %d: %s\n"), i, dictionary->words[i]);
#endif

        // load word into hashmap for fast lookup
        word_map[dictionary->words[i]] = true;

        i++;
    }

    fclose(inputFile);
    return true;
}

/**
 * Create and start the three main server threads
 * - game: Main game logic and letter generation
 * - _listen: Client connection handling
 * - cli: Administrative command line interface
 *
 * @return true if all threads created successfully, false otherwise
 */
bool initThreads() {

    if ((game_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)game, NULL, 0, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateThread %d"), GetLastError());
        return false;
    }

    if ((listen_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)_listen, NULL, 0, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateThread %d"), GetLastError());
        return false;
    }

    if ((cli_thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)cli, NULL, 0, NULL)) == NULL)
    {
        _tprintf(TEXT("CreateThread %d"), GetLastError());
        return false;
    }
}

/* handle message procedures */

/**
 * Handle player login request
 * Thread-safe wrapper around GameData::insert()
 *
 * @param name Player name attempting to login
 * @return Login_Return_Type containing result flag and assigned player ID
 */
Login_Return_Type handleLogin(const TCHAR* name) {

    WaitForSingleObject(data_handle, INFINITE);  // Acquire exclusive access to GameData

    Login_Return_Type res = data.insert(name);  // Try to insert new player entry

    ReleaseMutex(data_handle);                   // Release GameData access
    
    // Announce new player, if accepted
    if (res.flag == LOGIN) {
        Packet p = { 0 };
        p.code = PLAYER_LOGIN;
        _tcscpy_s(p.buffer, (ARRAY_SIZE + 2) * sizeof(TCHAR), name);

        WaitForSingleObject(data_handle, INFINITE);  // Acquire exclusive access to GameData
        
        data.broadcast(p, res.id);                           // Announce new player
        
        ReleaseMutex(data_handle);                   // Release GameData access
    }

    return res;
}

Packet handleScoreRequest(int32_t id) {
    
    int32_t score = 0;
    Packet p = { 0 };
    p.code = SCORE;

    WaitForSingleObject(data_handle, INFINITE);
    score = data.score(id);
    ReleaseMutex(data_handle);

    p.id = score < 0 ? 0 : score;

    return p;
}

/**
 * Handle player logout by name
 * Removes player from GameData and broadcasts departure to all clients
 *
 * @param name Name of player logging out
 */
void handleLogout(const TCHAR* name) {
    Packet p = { 0 };
    p.code = PLAYER_LOGOUT;
    _tcsncpy_s(p.buffer, name, ARRAY_SIZE + 1);

    Packet exit_order;
    ZeroMemory(&exit_order, sizeof(Packet));
    exit_order.code = LOGOUT;

    std::wcout << L"Removing " << name << L"\n";
    
    // Thread-safe player removal
    WaitForSingleObject(data_handle, INFINITE);

    int32_t player_id = data.byName(name);

    if (player_id != -1) 
    {
        data.send(player_id, exit_order);
        data.remove(name);
        data.broadcast(p, player_id);  // Announce departure to all connected clients

    }
    
    ReleaseMutex(data_handle);
}

/**
 * Handle player logout by ID
 * Alternative logout method using player ID instead of name
 *
 * @param id Player ID logging out
 */
void handleLogout(const int32_t id) {
    Packet p = { 0 };
    p.code = PLAYER_LOGOUT;
    const TCHAR* name = data.playerName(id);

    if (name == NULL) { // Player does not exist
        return;
    }

    Packet exit_order = { 0 };
    exit_order.code = LOGOUT;

    std::wcout << L"Removing " << name << L" ID: " << id << L"\n";
    _tcsncpy_s(p.buffer, name, ARRAY_SIZE + 1);

    // Thread-safe player removal
    WaitForSingleObject(data_handle, INFINITE);
    
    data.send(id, exit_order);
    data.remove(name);
    data.broadcast(p);  // Announce departure to all connected clients

    ReleaseMutex(data_handle);
}

/**
 * Handle word guess from a player
 * Validates the guess against current letter array and updates score if correct
 * Uses both semaphore (for shared memory) and mutex (for GameData) synchronization
 *
 * @param gameId Player ID making the guess
 * @param buffer Word guess from the player
 */
void handleGuess(int32_t gameId, const TCHAR* buffer) {
    std::wstring guess;
    const TCHAR* word = buffer;
    const TCHAR* name = NULL;
    bool announceGuess = false;
    int32_t i = 0, score = 0;

    // Acquire shared memory access and GameData access
    WaitForSingleObject(semaphore_handle, INFINITE);
    WaitForSingleObject(data_handle, INFINITE);

    // Validate player exists
    if ((name = data.playerName(gameId)) == NULL) {
#ifdef DEBUG
        std::cout << "Player does not exist. ID: " << gameId << "\n";
#endif
        ReleaseSemaphore(semaphore_handle, 1, NULL);
        ReleaseMutex(data_handle);
        return;
    }

    // Convert buffer to wstring for processing
    for (; i < BUFFER_SIZE; ++i) {
        if (word[i] == TEXT('\0')) {
            break;
        }

        guess += (char)buffer[i];
    }
    
    // Check if guess is valid (non-empty and matches available letters)
    if (!guess.empty() && word_match(guess.c_str(), state->array))
    {
        data.update(gameId, 1);     // Award point to player

        announceGuess = true;       // Boolean flag for announcing guess

        score = data.score(gameId); // For announcing new score

        SetEvent(clear_handle);     // Signal game thread to clear array
    }

    // Announce if a word has been guessed (and who guessed it)
    if (announceGuess) {
        Packet p = { 0 };
        p.code = GUESS;
        p.id = score;
        _tcsncpy_s(p.buffer, name, ARRAY_SIZE + 1);

        data.broadcast(p);
    }

    // Release GameData
    ReleaseMutex(data_handle);
    // Release semaphore
    ReleaseSemaphore(semaphore_handle, 1, NULL);

    
    return;
}

/* aux procedures */

/**
 * Display the current letter array to console
 * Shows underscores for empty positions and letters for filled positions
 *
 * @param array Letter array to display
 */
void display(const TCHAR* array, int arraySize) {
    for (int i = 0; i < arraySize; ++i) {
        if (array[i] == 0) {
            std::wcout << L" _ ";  // Empty position
        }
        else std::wcout << L" " << array[i] << L" ";  // Letter
    }

    std::wcout << L"\n";
}

/**
 * Clear/reset the letter array
 * Sets all positions to null character
 *
 * @param array Array to clear
 */
void clear(TCHAR* array) {
    memset(array, 0, ARRAY_SIZE * sizeof(TCHAR));
}

/**
 * Check if a word can be formed from available letters
 * Uses frequency counting to ensure word doesn't use more letters than available
 *
 * @param input Word to check
 * @param array Available letters array
 * @return true if word can be formed, false otherwise
 * @throws std::runtime_error if either parameter is NULL
 */
bool word_match(const TCHAR* input, const TCHAR* array) {
    if (array == NULL || input == NULL) {
        throw std::runtime_error("array == NULL || input == NULL");
    }

    int t[26] = { 0 };  // Frequency table for letters a-z
    int len = _tcslen(input);

    // Count available letters
    for (int i = 0; i < ARRAY_SIZE; ++i) {
        t[array[i] - L'a'] += 1;
    }

    // Check if input word can be formed
    for (int i = 0; i < len; ++i) {
        TCHAR c = input[i];
        int f = t[c - L'a'];

        f -= 1;
        if (f < 0) {
            return false;  // Not enough of this letter available
        }

        t[c - L'a'] = f;
    }

    std::wstring temp(input);

    // input exists in the dictionary
    if (word_map.find(temp) != word_map.end()) {
        return true;
    }
    else {
        // close, but no cigar
        return false;
    }
}

/**
 * Initialize command map for CLI interface
 * Creates lambda functions for each administrative command
 * @param cmds Reference to command map to populate
 */
void initCmds(std::map<std::wstring, cmd>& cmds) {
    const HANDLE this_data_handle = data_handle;
    const HANDLE this_semaphore_handle = semaphore_handle;

    // "listar" - List all players and their scores
    cmds[TEXT("listar")] = [this_data_handle](const TCHAR* args) {
        WaitForSingleObject(this_data_handle, INFINITE);
        std::wcout << data.str();  // Display leaderboard
        ReleaseMutex(this_data_handle);
        };

    // "excluir" - Exclude/remove a player by name
    cmds[TEXT("excluir")] = [this_data_handle](const TCHAR* args) {
        WaitForSingleObject(this_data_handle, INFINITE);
        handleLogout(args);
        ReleaseMutex(this_data_handle);
        std::wcout << TEXT("Goodbye ") << args << "\n";
        };

    // "acelerar" - Accelerate game (decrease time interval, minimum 1000ms)
    cmds[TEXT("acelerar")] = [this_semaphore_handle](const TCHAR* args) {
        WaitForSingleObject(this_semaphore_handle, INFINITE);
        INTERVAL = (INTERVAL - 1000) < 1000 ? 1000 : INTERVAL - 1000;
        ReleaseSemaphore(this_semaphore_handle, 1, NULL);
        };

    // "travar" - Brake/slow down game (increase time interval)
    cmds[TEXT("travar")] = [this_semaphore_handle](const TCHAR* args) {
        WaitForSingleObject(this_semaphore_handle, INFINITE);
        INTERVAL = INTERVAL + 1000;
        ReleaseSemaphore(this_semaphore_handle, 1, NULL);
        };

    // "bot" - Launch bot process
    cmds[TEXT("bot")] = [this_data_handle](const TCHAR* args) {
        bool nameExists = false;
        int32_t len = _tcslen(args);

        if (len == 0 || len > ARRAY_SIZE + 2) {
            return;
        }

        WaitForSingleObject(this_data_handle, INFINITE);
        nameExists = data.playerExists(args);
    
        if (!nameExists) {
            STARTUPINFO si;
            PROCESS_INFORMATION pi;

            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            TCHAR botFullPath[BUFFER_SIZE * sizeof(TCHAR)];
            _stprintf_s(botFullPath, BUFFER_SIZE * sizeof(TCHAR), L"cmd.exe /c start %s %s -bot", botPath, args);

            if (!CreateProcess(  // launch bot instance
                NULL,
                botFullPath,
                NULL,
                NULL,
                FALSE,
                0,
                NULL,
                NULL,
                &si,
                &pi
            )) {
                std::cout << "bot CreateProcess " << GetLastError() << "\n";
                ReleaseSemaphore(this_data_handle, 1, NULL);
                return;
            }
        }

        ReleaseSemaphore(this_data_handle, 1, NULL);
     };
}

/* thread procedures */

/**
 * Main game thread - Core game logic
 * Continuously generates random letters and updates shared game state
 * Synchronizes with clients using semaphore mechanism
 *
 * @param param Unused thread parameter
 * @return NULL when thread exits
 */
void* game(void* param) {
    int i = 0;  // Current position in letter array (wraps around)
    uint32_t updated_interval;
    srand(time(NULL));      // Initialize random seed
    clear(state->array);    // Start with empty array
    state->t = LETTERS;     // Assign max array length

    while (WaitForSingleObject(quit_handle, 0) != WAIT_OBJECT_0)    // Continue until quit signal
    {
        // Lock entire system by acquiring all semaphore permits
        // This ensures no clients are reading shared memory during update
#ifdef DEBUG
        std::cout << "Waiting for semaphore..." << std::endl;
#endif

        for (int i = 0; i < MAX_PLAYERS + 2; ++i) {
            WaitForSingleObject(semaphore_handle, INFINITE);    // Acquire all locks, semaphore count -> 0
        }

#ifdef DEBUG
        std::cout << "Semaphore locked" << std::endl;
#endif

        // Check if array should be cleared (correct guess was made)
        if (WaitForSingleObject(clear_handle, 0) == WAIT_OBJECT_0) {
            clear(state->array);        // Reset letter array
            ResetEvent(clear_handle);   // Reset the clear signal
        }

        // Update game state with new random letter
        state->array[i] = (TCHAR)(L'a' + (TCHAR)(rand() % (L'z' + 1 - L'a')));
        updated_interval = INTERVAL;

#ifdef DEBUG
        display(state->array, state->t);
#endif

#ifdef DEBUG
        std::cout << "Updating..." << std::endl;
#endif

        data.updateAllClients();  // Signal all clients to refresh their game state
        ReleaseSemaphore(semaphore_handle, MAX_PLAYERS + 2, NULL);    // Release all permits, allow client access

#ifdef DEBUG
        std::cout << "Release semaphore" << std::endl;
#endif

        i = (i + 1) % state->t;          // Move to next position (circular)
        Sleep(updated_interval);         // Wait before next letter
    }

#ifdef DEBUG
    std::cout << "Thread " << __func__ << " exiting\n";
#endif

    return NULL;
}

/**
 * Client connection listener thread
 * Handles incoming client connections via named pipes
 * Processes LOGIN, LOGOUT, and GUESS packets
 * Uses overlapped I/O for non-blocking pipe operations
 *
 * @param param Unused thread parameter
 * @return NULL when thread exits
 */
void* _listen(void* param)
{
    Packet input[1];                // Buffer for incoming packets
    Packet output[1];               // Buffer for outgoing packets
    HANDLE pipe_handle;             // Named pipe handle for client communication
    Login_Return_Type response;     // Response structure for login attempts
    OVERLAPPED ol = { 0 };          // Overlapped structure for async operation
    bool result;

    // Create event for overlapped operations
    if ((ol.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL)) == NULL) {
        _tprintf(TEXT("CreateEvent %d"), GetLastError());
        SetEvent(quit_handle);      // Signal shutdown on error
        return NULL;
    }

    // Create named pipe for client connections
    if ((pipe_handle = CreateNamedPipe(
        serverPipeName,                                                                    // Pipe name
        PIPE_ACCESS_DUPLEX | PIPE_TYPE_BYTE | FILE_FLAG_OVERLAPPED, // Access and flags
        PIPE_WAIT,                                                                         // Pipe mode
        PIPE_UNLIMITED_INSTANCES,                                                          // Max instances
        2 * BUFFER_SIZE * sizeof(TCHAR),                                                   // Output buffer size
        2 * BUFFER_SIZE * sizeof(TCHAR),                                                   // Input buffer size
        0,                                                                                 // Default timeout
        NULL)) == INVALID_HANDLE_VALUE)                                                    // Security attributes
    {
        _tprintf(TEXT("CreateNamedPipe %d"), GetLastError());
        CloseHandle(ol.hEvent);
        SetEvent(quit_handle);  // Signal shutdown on error
        return NULL;
    }

    std::cout << "Waiting connection..." << std::endl;

    while (WaitForSingleObject(quit_handle, 0) != WAIT_OBJECT_0)    // Continue until quit signal
    {
        // Wait for client connection (with timeout to allow checking quit signal)
        result = ConnectNamedPipe(pipe_handle, &ol);

        if (!result) {
            DWORD err = GetLastError();

            if (err == ERROR_IO_PENDING)
            {
                // Async operation in progress, wait with timeout
                DWORD waitResult = WaitForSingleObject(ol.hEvent, 1000);

                if (waitResult == WAIT_TIMEOUT) {
                    continue;   // Timeout reached, check quit signal
                }
                // Client connected successfully within timeout
            }
            else if (err == ERROR_PIPE_CONNECTED) {
                // Client connected between pipe creation and ConnectNamedPipe call
                SetEvent(ol.hEvent); // Manually signal event
            }
            else {
                _tprintf(TEXT("ConnectNamedPipe %d"), err);
                SetEvent(quit_handle);
                break;
            }
        }

        // Read packet from connected client
        if ((ReadFile(pipe_handle, input, sizeof(Packet), NULL, NULL)) == 0) {
            _tprintf(TEXT("ReadFile %d"), GetLastError());
            SetEvent(quit_handle);

#ifdef DEBUG
            std::cout << "Thread " << __func__ << " exiting\n";
#endif
            break;
        }

        _tprintf_s(L"Packet received: (%d, %d, %s)\n", input->code, input->id, input->buffer);

        // Process packet based on type
        switch (input->code)
        {
        case LOGIN:
            // Handle new player login
            response = handleLogin((*input).buffer);

            if (!WriteFile(pipe_handle, &response, sizeof(Login_Return_Type), NULL, NULL)) {
                _tprintf(TEXT("WriteFile %d"), GetLastError());
            }
            break;

        case LOGOUT:
           // Handle player logout
            handleLogout((*input).id);
            break;

        case SCORE:
            // Handle score request

            output[0] = handleScoreRequest((*input).id);
            
            if (!WriteFile(pipe_handle, &output[0], sizeof(Packet), NULL, NULL)) {
                _tprintf(TEXT("WriteFile %d"), GetLastError());
            }

            break;

        case GUESS:
            // Handle word guess
            handleGuess((*input).id, (*input).buffer);

            // Send acknowledgment back to client
            if (!WriteFile(pipe_handle, input, sizeof(Packet), NULL, NULL)) {
                _tprintf(TEXT("WriteFile %d"), GetLastError());
            }
            break;

        default:
            std::cout << "\nUnexpected packet flag received: " << input->code << std::endl;
            break;
        }

        // Clean up connection for next client
        DisconnectNamedPipe(pipe_handle);
        FlushFileBuffers(pipe_handle);
    }

    // Cleanup on thread exit
    CloseHandle(ol.hEvent);
    CloseHandle(pipe_handle);

#ifdef DEBUG
    std::cout << "Thread " << __func__ << " exiting\n";
#endif
    return NULL;
}

/**
 * Command Line Interface thread
 * Provides administrative console interface for server management
 *
 * @param param Unused thread parameter
 * @return NULL when thread exits
 */
void* cli(void* param)
{
    std::wstring input, first, second;  // Command parsing variables
    std::map<std::wstring, cmd> cmds;   // Command function map
    initCmds(cmds);                     // Initialize available commands

    while (WaitForSingleObject(quit_handle, 0) != WAIT_OBJECT_0) {  // Continue until quit signal
        std::wcout << "\n > ";                      // Display prompt
        input = parseCmdline(first, second);        // Parse user input

        // Handle shutdown command
        if (input == L"encerrar") {
            std::cout << "Encerrando...\n";         // "Shutting down..."
            SetEvent(quit_handle);                  // Signal global shutdown

#ifdef DEBUG
            std::cout << "Thread " << __func__ << " exiting\n";
#endif
            return NULL;
        }

        // Execute command if it exists
        try {
            cmds[first](second.c_str());            // Call command function with arguments
        }
        catch (...) {
            std::wcout << L"Comando \"" << input << L"\" nao registado.\n";  // "Command not registered"
        }
    }

#ifdef DEBUG
    std::cout << "Thread " << __func__ << " exiting\n";
#endif

    return NULL;
}



/*
    For getting dword values from register, namely MAXLETRAS and RITMO
*/

int dwordFromRegistryKey(const TCHAR* subKey, const TCHAR* valueName) {
    HKEY hKey;
    DWORD dwData;
    DWORD dwSize = sizeof(dwData);
    DWORD dwType;

    // Open the registry key
    LONG result = RegOpenKeyEx(HKEY_CURRENT_USER, subKey, 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        std::cerr << "Failed to open registry key. Error code: " << result << std::endl;
        return -1;
    }

    // Query the DWORD value
    result = RegQueryValueEx(hKey, valueName, NULL, &dwType ,(LPBYTE) & dwData, &dwSize);
    if (result != ERROR_SUCCESS || dwType != REG_DWORD) {
        std::cerr << "Failed to read DWORD value. Error code: " << result << std::endl;
        RegCloseKey(hKey);
        return -1;
    }

    RegCloseKey(hKey);

    return dwData;
}
