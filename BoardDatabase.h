#ifndef BOARD_DATABASE_H
#define BOARD_DATABASE_H

#include <sqlite3.h>

#include <cstdint>
#include <string>

struct BoardState;

class BoardDatabase {
public:
    BoardDatabase();
    ~BoardDatabase();

    // Opens the database file at the provided path
    bool open(const std::string& databasePath);
    // Creates the tables and indexes used by board persistence
    bool initialize();
    // Loads a saved board and reports whether a row was found
    bool loadBoard(uint32_t boardID, BoardState& board, bool& found);
    // Saves a board and its objects in one transaction
    bool saveBoard(const BoardState& board);

    // Prevents multiple objects from owning the same database connection
    BoardDatabase(const BoardDatabase&) = delete;

    // Prevents assigning the database connection to another object
    BoardDatabase& operator=(const BoardDatabase&) = delete;

private:
    sqlite3* database = nullptr;

    bool executeSQL(const char* sql);
    // Rolls back the current database transaction
    bool rollbackTransaction();
};

#endif
