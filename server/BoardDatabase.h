#ifndef BOARD_DATABASE_H
#define BOARD_DATABASE_H

#include <sqlite3.h>

#include "BoardStructs.h"

#include <cstdint>
#include <map>
#include <string>

class BoardDatabase {
public:
    BoardDatabase();
    ~BoardDatabase();

    // Opens the database file at the provided path
    bool open(const std::string& databasePath);
    // Creates the tables and indexes used by board persistence
    bool initialize();
    // Reserves the next board ID without reusing deleted IDs
    bool getNextBoardID(uint32_t& boardID);
    // Loads a saved board and reports whether a row was found
    bool loadBoard(uint32_t boardID, BoardState& board, bool& found);
    // Saves a board and its objects in one transaction
    bool saveBoard(BoardState& board, bool updateEditDate = true);
    // Loads saved board names, IDs, and dates into an ordered map
    bool listBoards(BoardList& boardList);
    // Deletes one saved board and its cards and connectors
    bool deleteBoard(uint32_t boardID);

    // Prevents multiple objects from owning the same database connection
    BoardDatabase(const BoardDatabase&) = delete;

    // Prevents assigning the database connection to another object
    BoardDatabase& operator=(const BoardDatabase&) = delete;

private:
    sqlite3* database = nullptr;

    // Runs one SQL statement and reports any database error
    bool executeSQL(const char* sql);
    // Rolls back the current database transaction
    bool rollbackTransaction();
    // Assigns sequential names to saved boards that do not have one
    bool assignDefaultBoardNames();
    // Finds the next sequential default board name
    std::string nextDefaultBoardName(const BoardList& boardList) const;
};

#endif
