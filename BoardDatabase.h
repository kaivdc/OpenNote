#ifndef BOARD_DATABASE_H
#define BOARD_DATABASE_H

#include <sqlite3.h>

#include <string>

class BoardDatabase {
public:
    BoardDatabase();
    ~BoardDatabase();

    bool open(const std::string& databasePath);
    bool initialize();

    // Prevents multiple objects from owning the same database connection
    BoardDatabase(const BoardDatabase&) = delete;

    // Prevents assigning the database connection to another object
    BoardDatabase& operator=(const BoardDatabase&) = delete;

private:
    sqlite3* database = nullptr;

    bool executeSQL(const char* sql);
};

#endif
