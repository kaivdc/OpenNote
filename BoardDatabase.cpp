#include "BoardDatabase.h"
#include "BoardStructs.h"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

// Prepares one SQL statement and reports any database error
bool prepareStatement(sqlite3* database, const char* sql, sqlite3_stmt** statement) {
    if (sqlite3_prepare_v2(database, sql, -1, statement, nullptr) != SQLITE_OK) {
        std::cerr << "[ERROR] : Database query failed: " << sqlite3_errmsg(database) << "\n";
        return false;
    }
    return true;
}

// Binds the four color components to a prepared statement
bool bindColorValues(sqlite3_stmt* statement, int firstIndex, const Color& color) {
    return sqlite3_bind_int(statement, firstIndex, color.r) == SQLITE_OK &&
           sqlite3_bind_int(statement, firstIndex + 1, color.g) == SQLITE_OK &&
           sqlite3_bind_int(statement, firstIndex + 2, color.b) == SQLITE_OK &&
           sqlite3_bind_int(statement, firstIndex + 3, color.a) == SQLITE_OK;
}

// Reads four color components from a prepared statement
Color readColorValues(sqlite3_stmt* statement, int firstColumn) {
    return {
        static_cast<uint8_t>(sqlite3_column_int(statement, firstColumn)),
        static_cast<uint8_t>(sqlite3_column_int(statement, firstColumn + 1)),
        static_cast<uint8_t>(sqlite3_column_int(statement, firstColumn + 2)),
        static_cast<uint8_t>(sqlite3_column_int(statement, firstColumn + 3))
    };
}

// Reads a text value from a prepared statement without losing its byte length
std::string readTextValue(sqlite3_stmt* statement, int columnIndex) {
    const unsigned char* value = sqlite3_column_text(statement, columnIndex);
    const int byteCount = sqlite3_column_bytes(statement, columnIndex);
    if (value == nullptr || byteCount <= 0) {
        return std::string{};
    }
    return std::string(reinterpret_cast<const char*>(value), static_cast<size_t>(byteCount));
}

// Reads a positive 32-bit value from a prepared statement
bool readUint32Value(sqlite3_stmt* statement, int columnIndex, uint32_t& value) {
    const sqlite3_int64 storedValue = sqlite3_column_int64(statement, columnIndex);
    if (storedValue <= 0 || storedValue > std::numeric_limits<uint32_t>::max()) {
        return false;
    }
    value = static_cast<uint32_t>(storedValue);
    return true;
}

// Runs one prepared statement and clears it for the next row
bool runStatement(sqlite3* database, sqlite3_stmt* statement, const char* action) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        std::cerr << "[ERROR] : " << action << ": " << sqlite3_errmsg(database) << "\n";
        return false;
    }
    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);
    return true;
}

} // namespace

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

// Loads one board and its saved cards and connectors
bool BoardDatabase::loadBoard(uint32_t boardID, BoardState& board, bool& found) {
    found = false;
    board = BoardState{};
    board.boardID = boardID;
    if (database == nullptr || boardID == 0) {
        std::cerr << "[ERROR] : Database must be open and boardID must be positive to load a board.\n";
        return false;
    }

    const char* boardSQL = R"sql(
        SELECT next_card_id, next_connector_id,
               board_background_red, board_background_green, board_background_blue, board_background_alpha,
               card_red, card_green, card_blue, card_alpha,
               connector_red, connector_green, connector_blue, connector_alpha,
               accent_red, accent_green, accent_blue, accent_alpha,
               important_elements_red, important_elements_green, important_elements_blue, important_elements_alpha
        FROM boards WHERE board_id = ?;
    )sql";
    sqlite3_stmt* statement = nullptr;
    if (!prepareStatement(database, boardSQL, &statement)) {
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, boardID) != SQLITE_OK) {
        std::cerr << "[ERROR] : Could not bind boardID while loading a board.\n";
        sqlite3_finalize(statement);
        return false;
    }

    int result = sqlite3_step(statement);
    if (result == SQLITE_DONE) {
        sqlite3_finalize(statement);
        return true;
    }
    if (result != SQLITE_ROW) {
        std::cerr << "[ERROR] : Could not load board: " << sqlite3_errmsg(database) << "\n";
        sqlite3_finalize(statement);
        return false;
    }

    if (!readUint32Value(statement, 0, board.nextCardID) ||
        !readUint32Value(statement, 1, board.nextConnectorID)) {
        std::cerr << "[ERROR] : Board has invalid saved ID values.\n";
        sqlite3_finalize(statement);
        return false;
    }
    board.theme.boardBackgroundColor = readColorValues(statement, 2);
    board.theme.cardColor = readColorValues(statement, 6);
    board.theme.connectorColor = readColorValues(statement, 10);
    board.theme.accentColor = readColorValues(statement, 14);
    board.theme.importantElements = readColorValues(statement, 18);
    sqlite3_finalize(statement);

    const char* cardSQL = R"sql(
        SELECT card_id, x, y, width, height, z_index, text, media_url,
               color_red, color_green, color_blue, color_alpha
        FROM cards WHERE board_id = ? ORDER BY z_index, card_id;
    )sql";
    statement = nullptr;
    if (!prepareStatement(database, cardSQL, &statement)) {
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, boardID) != SQLITE_OK) {
        std::cerr << "[ERROR] : Could not bind boardID while loading cards.\n";
        sqlite3_finalize(statement);
        return false;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Card card{};
        if (!readUint32Value(statement, 0, card.cardID) ||
            !readUint32Value(statement, 5, card.zIndex)) {
            std::cerr << "[ERROR] : Board contains a card with an invalid ID.\n";
            sqlite3_finalize(statement);
            return false;
        }
        card.x = static_cast<float>(sqlite3_column_double(statement, 1));
        card.y = static_cast<float>(sqlite3_column_double(statement, 2));
        card.width = static_cast<float>(sqlite3_column_double(statement, 3));
        card.height = static_cast<float>(sqlite3_column_double(statement, 4));
        if (!std::isfinite(card.x) || !std::isfinite(card.y) ||
            !std::isfinite(card.width) || !std::isfinite(card.height) ||
            card.width < 20.0f || card.width > 1000.0f ||
            card.height < 20.0f || card.height > 1000.0f) {
            std::cerr << "[ERROR] : Board contains a card with invalid geometry.\n";
            sqlite3_finalize(statement);
            return false;
        }
        card.text = readTextValue(statement, 6);
        card.mediaURL = readTextValue(statement, 7);
        card.color = readColorValues(statement, 8);
        board.cards[card.cardID] = card;
    }
    if (result != SQLITE_DONE) {
        std::cerr << "[ERROR] : Could not load cards: " << sqlite3_errmsg(database) << "\n";
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);

    const char* connectorSQL = R"sql(
        SELECT connector_id, source_card_id, target_card_id, thickness,
               color_red, color_green, color_blue, color_alpha
        FROM connectors WHERE board_id = ? ORDER BY connector_id;
    )sql";
    statement = nullptr;
    if (!prepareStatement(database, connectorSQL, &statement)) {
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, boardID) != SQLITE_OK) {
        std::cerr << "[ERROR] : Could not bind boardID while loading connectors.\n";
        sqlite3_finalize(statement);
        return false;
    }

    while ((result = sqlite3_step(statement)) == SQLITE_ROW) {
        Connector connector{};
        if (!readUint32Value(statement, 0, connector.connectorID) ||
            !readUint32Value(statement, 1, connector.sourceCardID) ||
            !readUint32Value(statement, 2, connector.targetCardID)) {
            std::cerr << "[ERROR] : Board contains a connector with an invalid ID.\n";
            sqlite3_finalize(statement);
            return false;
        }
        connector.thickness = static_cast<float>(sqlite3_column_double(statement, 3));
        if (!std::isfinite(connector.thickness) || connector.thickness < 0.1f || connector.thickness > 1000.0f) {
            std::cerr << "[ERROR] : Board contains a connector with invalid thickness.\n";
            sqlite3_finalize(statement);
            return false;
        }
        connector.color = readColorValues(statement, 4);
        board.connectors[connector.connectorID] = connector;
    }
    if (result != SQLITE_DONE) {
        std::cerr << "[ERROR] : Could not load connectors: " << sqlite3_errmsg(database) << "\n";
        sqlite3_finalize(statement);
        return false;
    }
    sqlite3_finalize(statement);
    found = true;
    return true;
}

// Saves one board and replaces its saved cards and connectors in one transaction
bool BoardDatabase::saveBoard(const BoardState& board) {
    if (database == nullptr || !board.isValid()) {
        std::cerr << "[ERROR] : Database must be open and board must be valid to save a board.\n";
        return false;
    }
    if (!executeSQL("BEGIN TRANSACTION;")) {
        return false;
    }

    sqlite3_stmt* statement = nullptr;
    if (!prepareStatement(database, "INSERT OR IGNORE INTO boards (board_id) VALUES (?);", &statement)) {
        rollbackTransaction();
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, board.boardID) != SQLITE_OK ||
        !runStatement(database, statement, "Could not create board record")) {
        if (sqlite3_errcode(database) == SQLITE_OK) {
            std::cerr << "[ERROR] : Could not bind boardID while saving a board.\n";
        }
        sqlite3_finalize(statement);
        rollbackTransaction();
        return false;
    }
    sqlite3_finalize(statement);

    const char* updateBoardSQL = R"sql(
        UPDATE boards SET next_card_id = ?, next_connector_id = ?,
            board_background_red = ?, board_background_green = ?, board_background_blue = ?, board_background_alpha = ?,
            card_red = ?, card_green = ?, card_blue = ?, card_alpha = ?,
            connector_red = ?, connector_green = ?, connector_blue = ?, connector_alpha = ?,
            accent_red = ?, accent_green = ?, accent_blue = ?, accent_alpha = ?,
            important_elements_red = ?, important_elements_green = ?, important_elements_blue = ?, important_elements_alpha = ?
        WHERE board_id = ?;
    )sql";
    statement = nullptr;
    if (!prepareStatement(database, updateBoardSQL, &statement)) {
        rollbackTransaction();
        return false;
    }
    const bool boardValuesBound =
        sqlite3_bind_int64(statement, 1, board.nextCardID) == SQLITE_OK &&
        sqlite3_bind_int64(statement, 2, board.nextConnectorID) == SQLITE_OK &&
        bindColorValues(statement, 3, board.theme.boardBackgroundColor) &&
        bindColorValues(statement, 7, board.theme.cardColor) &&
        bindColorValues(statement, 11, board.theme.connectorColor) &&
        bindColorValues(statement, 15, board.theme.accentColor) &&
        bindColorValues(statement, 19, board.theme.importantElements) &&
        sqlite3_bind_int64(statement, 23, board.boardID) == SQLITE_OK;
    if (!boardValuesBound || !runStatement(database, statement, "Could not save board theme and IDs")) {
        if (!boardValuesBound) {
            std::cerr << "[ERROR] : Could not bind board data.\n";
        }
        sqlite3_finalize(statement);
        rollbackTransaction();
        return false;
    }
    sqlite3_finalize(statement);

    const char* deleteConnectorsSQL = "DELETE FROM connectors WHERE board_id = ?;";
    statement = nullptr;
    if (!prepareStatement(database, deleteConnectorsSQL, &statement)) {
        rollbackTransaction();
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, board.boardID) != SQLITE_OK ||
        !runStatement(database, statement, "Could not replace saved connectors")) {
        sqlite3_finalize(statement);
        rollbackTransaction();
        return false;
    }
    sqlite3_finalize(statement);

    const char* deleteCardsSQL = "DELETE FROM cards WHERE board_id = ?;";
    statement = nullptr;
    if (!prepareStatement(database, deleteCardsSQL, &statement)) {
        rollbackTransaction();
        return false;
    }
    if (sqlite3_bind_int64(statement, 1, board.boardID) != SQLITE_OK ||
        !runStatement(database, statement, "Could not replace saved cards")) {
        sqlite3_finalize(statement);
        rollbackTransaction();
        return false;
    }
    sqlite3_finalize(statement);

    const char* insertCardSQL = R"sql(
        INSERT INTO cards (board_id, card_id, x, y, width, height, z_index, text, media_url,
                           color_red, color_green, color_blue, color_alpha)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql";
    statement = nullptr;
    if (!prepareStatement(database, insertCardSQL, &statement)) {
        rollbackTransaction();
        return false;
    }
    for (const auto& entry : board.cards) {
        const Card& card = entry.second;
        const bool cardValuesBound =
            sqlite3_bind_int64(statement, 1, board.boardID) == SQLITE_OK &&
            sqlite3_bind_int64(statement, 2, card.cardID) == SQLITE_OK &&
            sqlite3_bind_double(statement, 3, card.x) == SQLITE_OK &&
            sqlite3_bind_double(statement, 4, card.y) == SQLITE_OK &&
            sqlite3_bind_double(statement, 5, card.width) == SQLITE_OK &&
            sqlite3_bind_double(statement, 6, card.height) == SQLITE_OK &&
            sqlite3_bind_int64(statement, 7, card.zIndex) == SQLITE_OK &&
            sqlite3_bind_text(statement, 8, card.text.data(), static_cast<int>(card.text.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
            sqlite3_bind_text(statement, 9, card.mediaURL.data(), static_cast<int>(card.mediaURL.size()), SQLITE_TRANSIENT) == SQLITE_OK &&
            bindColorValues(statement, 10, card.color);
        if (!cardValuesBound || !runStatement(database, statement, "Could not save card")) {
            if (!cardValuesBound) {
                std::cerr << "[ERROR] : Could not bind card data.\n";
            }
            sqlite3_finalize(statement);
            rollbackTransaction();
            return false;
        }
    }
    sqlite3_finalize(statement);

    const char* insertConnectorSQL = R"sql(
        INSERT INTO connectors (board_id, connector_id, source_card_id, target_card_id, thickness,
                                color_red, color_green, color_blue, color_alpha)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
    )sql";
    statement = nullptr;
    if (!prepareStatement(database, insertConnectorSQL, &statement)) {
        rollbackTransaction();
        return false;
    }
    for (const auto& entry : board.connectors) {
        const Connector& connector = entry.second;
        const bool connectorValuesBound =
            sqlite3_bind_int64(statement, 1, board.boardID) == SQLITE_OK &&
            sqlite3_bind_int64(statement, 2, connector.connectorID) == SQLITE_OK &&
            sqlite3_bind_int64(statement, 3, connector.sourceCardID) == SQLITE_OK &&
            sqlite3_bind_int64(statement, 4, connector.targetCardID) == SQLITE_OK &&
            sqlite3_bind_double(statement, 5, connector.thickness) == SQLITE_OK &&
            bindColorValues(statement, 6, connector.color);
        if (!connectorValuesBound || !runStatement(database, statement, "Could not save connector")) {
            if (!connectorValuesBound) {
                std::cerr << "[ERROR] : Could not bind connector data.\n";
            }
            sqlite3_finalize(statement);
            rollbackTransaction();
            return false;
        }
    }
    sqlite3_finalize(statement);

    if (!executeSQL("COMMIT;")) {
        rollbackTransaction();
        return false;
    }
    return true;
}

// Rolls back the current database transaction
bool BoardDatabase::rollbackTransaction() {
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        std::cerr << "[ERROR] : Database rollback failed: "
                  << (errorMessage != nullptr ? errorMessage : sqlite3_errmsg(database)) << "\n";
        sqlite3_free(errorMessage);
        return false;
    }
    sqlite3_free(errorMessage);
    return true;
}

// Runs one SQL statement and reports any database error
bool BoardDatabase::executeSQL(const char* sql) {
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(database, sql, nullptr, nullptr, &errorMessage);
    if (result != SQLITE_OK) {
        std::cerr << "[ERROR] : Database operation failed: "
                  << (errorMessage != nullptr ? errorMessage : sqlite3_errmsg(database)) << "\n";
        sqlite3_free(errorMessage);
        return false;
    }
    sqlite3_free(errorMessage);
    return true;
}
