#pragma once

#ifndef _GAMEDATA_H_
#define _GAMEDATA_H_

#include "..\..\wordgame_common.h"
#include <iostream>
#include <windows.h>
#include <tchar.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <set>
#include <map>
#include <sstream>
#include <functional>

/**
 * Simple ID generator for creating unique player IDs
 * Uses incrementing counter starting from 0
 */
struct Player_ID_Generator {
    int32_t state;  // Current ID counter

    Player_ID_Generator() : state(0) {};

    /**
     * Generate next unique player ID
     * @return Next available ID (incremented from previous)
     */
    int32_t gen() {
        state += 1;
        return state;
    }
};

// Global player ID generator instance
static Player_ID_Generator pid_gen;

/**
 * Represents a connected player in the game
 * Contains identification, score, and communication handles
 */
struct Player {
    int32_t id;                              // Unique player identifier
    int32_t score;                           // Current player score
    TCHAR pipe_name[2 * ARRAY_SIZE + 2];    // Named pipe path for client communication
    HANDLE update_handle;                    // Event handle for notifying client of updates
};

/**
 * Main game data management class
 * Handles player registration, scoring, and client communication
 * Maintains multiple data structures for efficient lookups:
 * - id_map: ID -> player name mapping
 * - score_map: score -> player name mapping (sorted by score descending)
 * - name_map: player name -> Player object mapping
 */
class GameData {
    // Maps player ID to player name for quick ID-based lookups
    std::map<int32_t, std::wstring> id_map;

    // Maps score to player name, sorted by score (highest first) for leaderboard
    std::multimap<int32_t, std::wstring, std::greater<unsigned int>> score_map;

    // Maps player name to Player object for complete player data access
    std::map<std::wstring, Player> name_map;

    bool write(const TCHAR* name, Packet& p) {
        // Connect to client's named pipe
        bool res;
        HANDLE pipeHandle = CreateFile(
            name,
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
            return false;  // Skip this client if connection fails
        }

        // Send packet and wait for acknowledgment
        if(!WriteFile(pipeHandle, &p, sizeof(Packet), NULL, NULL)){
#ifdef DEBUG
            std::cout << __func__ << " ";
            _tprintf(L"WriteFile %d\n", GetLastError());
#endif
            return false;
        }

        if (!ReadFile(pipeHandle, &res, sizeof(bool), NULL, NULL)) {
#ifdef DEBUG
            std::cout << __func__ << " ";
            _tprintf(L"ReadFile %d\n", GetLastError());
#endif
            return false;
        }

        CloseHandle(pipeHandle);
        return true;
    }

public:
    /**
     * Destructor - Clean up all player event handles
     */
    ~GameData() {
        for (auto& p : name_map) {
            CloseHandle(p.second.update_handle);
        }
    }

    /**
     * Register a new player in the game
     * Validates player name availability, server capacity, and client setup
     *
     * @param name Player's chosen name (must be unique)
     * @param initial_score Starting score (default 0)
     * @return Login_Return_Type containing success/failure flag and assigned player ID
     */
    Login_Return_Type insert(const TCHAR* name, const int32_t initial_score = 0)
    {
        Player p = { 0 };
        HANDLE update_event, pipe_handle = 0;
        TCHAR temp[2 * ARRAY_SIZE + 2] = { 0 };
        Login_Return_Type return_type = { 0 };

        // Check if player name is already taken
        if (playerExists(name)) {
            return_type.flag = NAME_USED;
            return_type.id = -1;
            return return_type;
        }

        // Check if server has reached maximum player capacity
        if (name_map.size() >= MAX_PLAYERS) {
            return_type.flag = SERVER_FULL;
            return_type.id = -1;
            return return_type;
        }

        // Try to open the client's update event handle
        // Format: "Local\\<playername>_update"
        _stprintf_s(temp, TEXT("%s%s%s"), TEXT("Local\\"), name, TEXT("_update"));
        update_event = OpenEvent(EVENT_ALL_ACCESS, FALSE, temp);

        if (!update_event) {
            return_type.flag = NO_EVENT;
            return_type.id = -1;
            return return_type;
        }

        // Verify client's named pipe exists and is available
        // Format: "\\\\.\\pipe\\<playername>"
        _stprintf_s(p.pipe_name, TEXT("%s%s"), TEXT("\\\\.\\pipe\\"), name);

        if (!WaitNamedPipe(p.pipe_name, 0))
        {
            return_type.flag = NO_PIPE;
            return_type.id = -1;
            return return_type;
        }

        // Initialize player data
        p.id = pid_gen.gen();           // Generate unique ID
        p.score = initial_score;        // Set initial score
        p.update_handle = update_event; // Store event handle for updates

        // Add player to all tracking data structures
        std::wstring n(name);
        this->name_map[n] = p;                                                    // Name -> Player mapping
        this->id_map[p.id] = n;                                                  // ID -> Name mapping
        this->score_map.insert(std::pair<int32_t, std::wstring>(initial_score, n)); // Score -> Name mapping

        // Return success with assigned player ID
        return_type.flag = LOGIN;
        return_type.id = p.id;

        return return_type;
    }

    /**
     * Remove player by name
     * Cleans up all references in id_map, score_map, and name_map
     *
     * @param name Player name to remove
     * @return true if player was found and removed, false otherwise
     */
    bool remove(const TCHAR* name) {
        std::wstring player_name(name);

        // Find player in name_map
        std::map<std::wstring, Player>::iterator itr1 = name_map.find(player_name);

        if (itr1 != name_map.end()) {
            int id = itr1->second.id;
            int score = itr1->second.score;
            name_map.erase(itr1);  // Remove from name_map

            // Remove from id_map
            std::map<int32_t, std::wstring>::iterator itr = id_map.find(id);
            if (itr != id_map.end()) {
                id_map.erase(itr);

                // Remove from score_map (need to find correct entry since scores may not be unique)
                auto range = score_map.equal_range(score);
                for (auto i = range.first; i != range.second; ++i) {
                    if (i->second == player_name) {
                        score_map.erase(i);
                        return true;
                    }
                }
                return false;
            }
            return false;
        }
        return false;
    }

    /**
     * Remove player by ID
     * Alternative removal method using player ID instead of name
     *
     * @param id Player ID to remove
     * @return true if player was found and removed, false otherwise
     */
    bool remove(int32_t id) {
        // Find player name by ID
        std::map<int32_t, std::wstring>::iterator itr = id_map.find(id);

        if (itr != id_map.end()) {
            std::wstring player_name = (*itr).second;
            id_map.erase(id);  // Remove from id_map

            // Remove from name_map
            std::map<std::wstring, Player>::iterator itr1 = name_map.find(player_name); // map<wstring, Player>::find(&name_map, player_name)
            if (itr1 != name_map.end()) {
                int score = itr1->second.score;
                name_map.erase(player_name);

                // Remove from score_map
                auto range = score_map.equal_range(score);
                for (auto i = range.first; i != range.second; ++i) {
                    if (i->second == player_name) {
                        score_map.erase(i);
                        return true;
                    }
                }
                return false;
            }
            return false;
        }
        return false;
    }

    /**
     * Check if a player with given name already exists
     *
     * @param name Player name to check
     * @return true if player exists, false otherwise
     */
    bool playerExists(const TCHAR* name) {
        std::wstring player_name(name);
        return (name_map.find(player_name) != name_map.end());
    }

    /**
     * Update player score by ID
     * Adjusts score and updates score_map for leaderboard consistency
     * Score cannot go below 0
     *
     * @param id Player ID to update
     * @param increment Amount to add to current score (can be negative)
     * @return true if update successful, false if player not found
     */
    bool update(const int32_t id, const int32_t increment) {
        // Find player by ID
        std::map<int32_t, std::wstring>::iterator itr = id_map.find(id);
        if (itr == id_map.end()) {
            return false;
        }

        auto player_name = (*itr).second;

        // Get player data
        std::map<std::wstring, Player>::iterator itr1 = name_map.find(player_name);
        if (itr1 == name_map.end()) {
            remove((*itr1).second.id);  // Clean up inconsistent data
            return false;
        }

        // Calculate new score (minimum 0)
        int score = itr1->second.score;
        int new_score = (score + increment < 0) ? 0 : score + increment;
        itr1->second.score = new_score;

        // Update score_map: remove old entry and add new one
        auto range = score_map.equal_range(score);
        for (auto i = range.first; i != range.second; ++i) {
            if (i->second == player_name) {
                score_map.erase(i);
                score_map.insert(std::pair<int32_t, std::wstring>(new_score, player_name));
                return true;
            }
        }
        return false;
    }

    /**
     * Update player score by name
     * Alternative score update method using player name
     *
     * @param name Player name to update
     * @param increment Amount to add to current score (can be negative)
     * @return true if update successful, false if player not found
     */
    bool update(const TCHAR* name, const int32_t increment) {
        auto player_name = std::wstring(name);

        // Find player by name
        std::map<std::wstring, Player>::iterator itr1 = name_map.find(player_name);
        if (itr1 == name_map.end()) {
            remove((*itr1).second.id);  // Clean up inconsistent data
            return false;
        }

        // Calculate new score (minimum 0)
        int score = itr1->second.score;
        int new_score = (score + increment < 0) ? 0 : score + increment;
        itr1->second.score = new_score;

        // Update score_map: remove old entry and add new one
        auto range = score_map.equal_range(score);
        for (auto i = range.first; i != range.second; ++i) {
            if (i->second == player_name) {
                score_map.erase(i);
                score_map.insert(std::pair<int32_t, std::wstring>(new_score, player_name));
                return true;
            }
        }
        return false;
    }

    int32_t score(int32_t gameId) {

        std::map<int32_t, std::wstring>::iterator itr = id_map.find(gameId);
        
        if (itr != id_map.end()) {  // Found id in id_map

            std::wstring& name = (itr)->second;
            std::map<std::wstring, Player>::iterator itr2 = name_map.find(name);

            if (itr2 != name_map.end()) {   // Found name in name_map

                Player& p = itr2->second;
                return p.score;

            }

            return -1;

        }

        // Player not found
        return -1;
    }

    /**
     * Generate leaderboard string
     * Creates formatted string showing player names and scores in descending order
     *
     * @param n Maximum number of players to include (-1 for all players)
     * @return Formatted leaderboard string
     */
    std::wstring str(int32_t n = -1) const {
        std::wstringstream ss;

        // Iterate through score_map (already sorted by score descending)
        for (auto& pr : score_map) {
            if (n == 0) break;
            ss << L"Nome: " << pr.second << L" Pontuação: " << pr.first << L"\n";
            n -= 1;
        }

        return ss.str();
    }

    /**
     * Notify all connected clients of updates
     * Triggers the update event for each player to refresh their game state
     */
    void updateAllClients() const {
        for (auto& pr : name_map) {
            const Player& p = pr.second;
            SetEvent(p.update_handle);  // Signal client to refresh
        }
    }

    /**
     * Send logout notification to all clients
     * Broadcasts LOGOUT packet to inform all players to leave
     */
    void warnLeave() const {
        Packet packt = { 0, 0, {0} };
        packt.code = LOGOUT;
        broadcast(packt);
    }

    /**
     * Send packet to all connected clients
     * Establishes connection to each client's named pipe and sends the packet
     *
     * @param p Packet to broadcast to all clients
     */
    void broadcast(Packet& p, int32_t except = -1) const {
        bool res;  // Response from client

        for (auto& pr : this->name_map) {

            const Player& player = pr.second;

            if (except != -1 && player.id == except) {
                continue;   // Exception, won't be notified
            }

#ifdef DEBUG
            std::wcout << L"Broadcasting to " << pr.first << L" at " << pr.second.pipe_name << L"\n";
#endif
            
            // Connect to client's named pipe
            HANDLE pipeHandle = CreateFile(
                player.pipe_name,
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
                continue;  // Skip this client if connection fails
            }

            // Send packet and wait for acknowledgment
            if (!WriteFile(pipeHandle, &p, sizeof(Packet), NULL, NULL)) {
#ifdef DEBUG
                std::cout << __func__ << " ";
                _tprintf(L"WriteFile %d\n", GetLastError());
#endif
                continue;
            }

            if (!ReadFile(pipeHandle, &res, sizeof(bool), NULL, NULL)) {
#ifdef DEBUG
                std::cout << __func__ << " ";
                _tprintf(L"ReadFile %d\n", GetLastError());
#endif
                continue;
            }

            CloseHandle(pipeHandle);
        }
    }

    int32_t byName(const TCHAR* name) const {
        if (name_map.find(name) != name_map.end()) {
            // Player by name exists
            return name_map.find(name)->second.id;
        }

        return -1;
    }

     
    bool send(const int32_t id, const Packet& p) const {    
        auto itr = id_map.find(id);

        if (itr != id_map.end()) {
            auto itr2 = name_map.find(itr->second);
            
            if (itr2 != name_map.end()) {

                const Player& player = itr2->second;
                bool res;

                // Connect to client's named pipe
                HANDLE pipeHandle = CreateFile(
                    player.pipe_name,
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
                    CloseHandle(pipeHandle);
                    return false;  // Skip this client if connection fails
                }

                // Send packet and wait for acknowledgment
                if (!WriteFile(pipeHandle, &p, sizeof(Packet), NULL, NULL)) {
#ifdef DEBUG
                    std::cout << __func__ << " ";
                    _tprintf(L"WriteFile %d\n", GetLastError());
#endif

                    CloseHandle(pipeHandle);
                    return false;
                }
   
                CloseHandle(pipeHandle);
                return true;
            }


            return false;

        }

        return false;
    } 


    /**
     * Get player name by ID
     * Safe lookup that handles invalid IDs gracefully
     *
     * @param id Player ID to look up
     * @return Player name if found, NULL if ID doesn't exist
     */
    const TCHAR* playerName(int32_t id) const {
        auto itr = id_map.find(id);
        
        if (itr != id_map.end()) {
            
            auto itr1 = name_map.find(itr->second);

            if (itr1 != name_map.end()) {

                return itr1->second.pipe_name;
            }

            return NULL;
        }

        return NULL;
    }

    /**
     * Get current number of connected players
     *
     * @return Number of players currently in the game
     */
    int32_t count() const {
        return this->name_map.size();
    }


};

#endif