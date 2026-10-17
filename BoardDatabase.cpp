#include "BoardDatabase.h"

#include <iostream>

// Creates a database object without opening a file
BoardDatabase::BoardDatabase() = default;

// Closes the database connection when the object is destroyed
BoardDatabase::~BoardDatabase() {
    if (database != nullptr) {
        sqlite3_close(database);
    }
}

// Opens the database file at the given path
bool BoardDatabase::open(const std::string& databasePath) {
    if (database != nullptr) {
        std::cerr << "[ERROR] : Database is already open.\n";
        return false;
    }

    if (sqlite3_open(databasePath.c_str(), &database) != SQLITE_OK) {
        std::cerr << "[ERROR] : Could not open database: "
                  << (database != nullptr ? sqlite3_errmsg(database) : "Unknown error") << "\n";
        if (database != nullptr) {
            sqlite3_close(database);
            database = nullptr;
        }
        return false;
    }
    return true;
}

// Creates the tables used to store boards, cards, and connectors
bool BoardDatabase::initialize() {
    if (database == nullptr) {
        std::cerr << "[ERROR] : Database must be open before creating tables.\n";
        return false;
    }

    if (!executeSQL("PRAGMA foreign_keys = ON;")) {
        return false;
    }

    return executeSQL(R"sql(
        CREATE TABLE IF NOT EXISTS boards (
            board_id INTEGER PRIMARY KEY CHECK (board_id > 0),
            next_card_id INTEGER NOT NULL DEFAULT 1 CHECK (next_card_id > 0),
            next_connector_id INTEGER NOT NULL DEFAULT 1 CHECK (next_connector_id > 0),
            board_background_red INTEGER NOT NULL DEFAULT 30 CHECK (board_background_red BETWEEN 0 AND 255),
            board_background_green INTEGER NOT NULL DEFAULT 30 CHECK (board_background_green BETWEEN 0 AND 255),
            board_background_blue INTEGER NOT NULL DEFAULT 35 CHECK (board_background_blue BETWEEN 0 AND 255),
            board_background_alpha INTEGER NOT NULL DEFAULT 255 CHECK (board_background_alpha BETWEEN 0 AND 255),
            card_red INTEGER NOT NULL DEFAULT 86 CHECK (card_red BETWEEN 0 AND 255),
            card_green INTEGER NOT NULL DEFAULT 66 CHECK (card_green BETWEEN 0 AND 255),
            card_blue INTEGER NOT NULL DEFAULT 86 CHECK (card_blue BETWEEN 0 AND 255),
            card_alpha INTEGER NOT NULL DEFAULT 255 CHECK (card_alpha BETWEEN 0 AND 255),
            connector_red INTEGER NOT NULL DEFAULT 150 CHECK (connector_red BETWEEN 0 AND 255),
            connector_green INTEGER NOT NULL DEFAULT 150 CHECK (connector_green BETWEEN 0 AND 255),
            connector_blue INTEGER NOT NULL DEFAULT 170 CHECK (connector_blue BETWEEN 0 AND 255),
            connector_alpha INTEGER NOT NULL DEFAULT 255 CHECK (connector_alpha BETWEEN 0 AND 255),
            accent_red INTEGER NOT NULL DEFAULT 252 CHECK (accent_red BETWEEN 0 AND 255),
            accent_green INTEGER NOT NULL DEFAULT 129 CHECK (accent_green BETWEEN 0 AND 255),
            accent_blue INTEGER NOT NULL DEFAULT 74 CHECK (accent_blue BETWEEN 0 AND 255),
            accent_alpha INTEGER NOT NULL DEFAULT 255 CHECK (accent_alpha BETWEEN 0 AND 255),
            important_elements_red INTEGER NOT NULL DEFAULT 255 CHECK (important_elements_red BETWEEN 0 AND 255),
            important_elements_green INTEGER NOT NULL DEFAULT 255 CHECK (important_elements_green BETWEEN 0 AND 255),
            important_elements_blue INTEGER NOT NULL DEFAULT 255 CHECK (important_elements_blue BETWEEN 0 AND 255),
            important_elements_alpha INTEGER NOT NULL DEFAULT 255 CHECK (important_elements_alpha BETWEEN 0 AND 255)
        );

        CREATE TABLE IF NOT EXISTS cards (
            board_id INTEGER NOT NULL,
            card_id INTEGER NOT NULL CHECK (card_id > 0),
            x REAL NOT NULL,
            y REAL NOT NULL,
            width REAL NOT NULL CHECK (width BETWEEN 20 AND 1000),
            height REAL NOT NULL CHECK (height BETWEEN 20 AND 1000),
            z_index INTEGER NOT NULL CHECK (z_index > 0),
            text TEXT NOT NULL CHECK (length(CAST(text AS BLOB)) <= 4096),
            media_url TEXT NOT NULL DEFAULT '',
            color_red INTEGER NOT NULL CHECK (color_red BETWEEN 0 AND 255),
            color_green INTEGER NOT NULL CHECK (color_green BETWEEN 0 AND 255),
            color_blue INTEGER NOT NULL CHECK (color_blue BETWEEN 0 AND 255),
            color_alpha INTEGER NOT NULL CHECK (color_alpha BETWEEN 0 AND 255),
            PRIMARY KEY (board_id, card_id),
            FOREIGN KEY (board_id) REFERENCES boards(board_id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS connectors (
            board_id INTEGER NOT NULL,
            connector_id INTEGER NOT NULL CHECK (connector_id > 0),
            source_card_id INTEGER NOT NULL,
            target_card_id INTEGER NOT NULL,
            thickness REAL NOT NULL CHECK (thickness BETWEEN 0.1 AND 1000),
            color_red INTEGER NOT NULL CHECK (color_red BETWEEN 0 AND 255),
            color_green INTEGER NOT NULL CHECK (color_green BETWEEN 0 AND 255),
            color_blue INTEGER NOT NULL CHECK (color_blue BETWEEN 0 AND 255),
            color_alpha INTEGER NOT NULL CHECK (color_alpha BETWEEN 0 AND 255),
            PRIMARY KEY (board_id, connector_id),
            FOREIGN KEY (board_id) REFERENCES boards(board_id) ON DELETE CASCADE,
            FOREIGN KEY (board_id, source_card_id) REFERENCES cards(board_id, card_id) ON DELETE CASCADE,
            FOREIGN KEY (board_id, target_card_id) REFERENCES cards(board_id, card_id) ON DELETE CASCADE
        );

        CREATE UNIQUE INDEX IF NOT EXISTS unique_connector_pair
        ON connectors (board_id, min(source_card_id, target_card_id), max(source_card_id, target_card_id));
    )sql");
}

// Runs one SQL statement and reports any database error
bool BoardDatabase::executeSQL(const char* sql) {
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        std::cerr << "[ERROR] : Database setup failed: "
                  << (errorMessage != nullptr ? errorMessage : sqlite3_errmsg(database)) << "\n";
        sqlite3_free(errorMessage);
        return false;
    }
    sqlite3_free(errorMessage);
    return true;
}
