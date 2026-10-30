# OpenNote
OpenNote is a self-hostable virtual whiteboarding application consisting of a self-hostable server executable, and a client executable, capable of accessing any server and navigating its list of boards. These boards update as the client creates changes, exercising server-side authority and saving/loading form a database.
[Github Repo](https://github.com/kaivdc/OpenNote/)


## Team Members
Kairos VanDeCar
Derek Colie

## Features
- Create, join, refresh, and delete boards from the board selection screen
- View a board's name, ID, creation date, and most recent edit date
- Create, move, resize, edit, and delete cards
- Add an image to a card using an image URL or a dropped image file
- Create and edit connectors between cards
- Change the board theme and save a board with its current name or a new name
- Select and delete multiple cards and connectors
- Share board changes with clients connected to the same board (P3)
- Save board data in a SQLite database

## Repository layout
-  `client/` contains the desktop client, rendering, local command handling, and packet creation code.
-  `server/` contains the server, board command handling, database, and logging code.
-  `shared/` contains board and settings types used by both applications.
-  `imgui/`, `third_party/`, and `resources/` contain GUI, image-decoding, and icon resources.
- The Makefile, run and dependency scripts, and this README stay in the project root.

## Requirements
The current build uses C++20, GNU Make, and a C++ compiler. The following instructions are for Ubuntu or WSL with a graphical display such as WSLg.

On Ubuntu or WSL, run this from the project directory to install the build tools and system libraries:
```sh
./install-dependencies.sh
```
The project includes Dear ImGui source files under `imgui/` and `stb_image.h` under `third_party/`. The JSON library, GLFW, OpenGL, SQLite, OpenSSL, and `curl` are system dependencies. `curl` is only needed by the client when it loads an image from an HTTP or HTTPS URL.

The Makefile also has a macOS build path. That path requires a C++20 compiler, Make, GLFW, OpenSSL, SQLite, and nlohmann/json to be installed. Windows-native builds are not set up; use WSL for the current Linux build.

## Build
From the project directory, run:
```sh
make
```
This builds `OpenNoteServer` and `OpenNoteClient`. To remove generated executables and object files, run:
```sh
make  clean
```

## Run the client and server together
To build and start a local server and client together (easy test, connecting at <port> on <ip>), run:
```sh
./run.sh <port>
```
`run.sh` cleans and rebuilds the executables and object files each time. It starts the server, connects the client to `127.0.0.1`, and stops the server when the client window closes. It does not remove the database or server log.

## Run the client and server separately
Start the server in one terminal with <port>:
```sh
./OpenNoteServer <port> [listen_address]
```
Start the client in another terminal, connecting to <ip> via <port>:
```sh
./OpenNoteClient <ip> <port>
```
The server listens only on the local computer (`127.0.0.1`) by default. To let other computers connect, provide a trusted network address, such as `0.0.0.0` to listen on every network interface:
```sh
./OpenNoteServer 5432 0.0.0.0
```
OpenNote does not use authentication or encrypted connections. Only expose the server to a network you trust. Clients on another computer must use the server computer's reachable address and the selected port.

## Using OpenNote
1. Start the server and open one or more clients.
2. Use **Create Board** to make a board, or **Refresh Board List** and **Join Board** to open a saved board.
3. Use the board context menu to create a card, change the theme, or save the board. Use **Save Board As** to give it a different name.
4. Create connectors between cards, edit cards, and drag or resize objects. Other clients on that board receive accepted changes from the server.
5. Close a client normally to disconnect. Boards are saved when changes are accepted, when the last client leaves a board, and during server shutdown.

## Client/server protocol
The client and server communicate over TCP. Each packet has a 5-byte header followed by a JSON payload:
| Header part | Size | Meaning |
| --- | ---: | --- |
| Opcode | 1 byte | Message type |
| Payload length | 4 bytes | Unsigned byte count in network byte order |
| Payload | Variable | JSON text payload |

The payload limit is 10 MiB. Complete board snapshots must also fit within this limit. The server accepts up to 16 client connections at a time. It closes clients that leave a packet incomplete for 60 seconds, stay idle for 90 seconds, take over 10 seconds to receive a packet, or fill their 10 MiB outgoing queue. The client sends a heartbeat every 30 seconds to keep an idle session open. The receiver reads the header and payload fully, so one TCP read is not treated as one complete packet. Several packets may arrive in one stream.

Every board snapshot includes a board ID and revision. Accepted board updates include the same values. Clients ignore updates for another board or an older revision. If a client detects a missing revision, it requests a fresh snapshot.

| Opcode | Name | Purpose |
| --- | --- | --- |
| `0x00` | `HEARTBEAT` | Client heartbeat |
| `0x02` | `MOVE_CARD` | Change card position, size, or draw order |
| `0x03` | `EDIT_CARD_CONTENT` | Change card text, image URL, or color |
| `0x04` | `CREATE_CARD` | Create a card; server assigns its ID |
| `0x05` | `DELETE_CARD` | Delete a card and its attached connectors |
| `0x06` | `CREATE_CONNECTOR` | Connect two cards |
| `0x07` | `EDIT_CONNECTOR` | Change connector appearance |
| `0x08` | `DELETE_CONNECTOR` | Delete a connector |
| `0x09` | `SYNC_BOARD` | Join a board and receive its current state and revision |
| `0x0A` | `CLIENT_CONNECTED` | Notify board clients that a client joined |
| `0x0B` | `CLIENT_DISCONNECTED` | Notify board clients that a client left |
| `0x10` | `DISCONNECT` | End the client session |
| `0x11` | `EDIT_BOARD_THEME` | Change a board theme color |
| `0x12` | `DELETE_OBJECTS` | Delete selected cards and connectors |
| `0x13` | `BRING_CARD_TO_FRONT` | Raise a card in draw order |
| `0x14` | `SAVE_BOARD` | Save the current board, optionally with a new name |
| `0x15` | `BOARD_SAVED` | Confirm a successful board save |
| `0x16` | `LIST_BOARDS` | Request the available board list |
| `0x17` | `BOARD_LIST` | Return board names, IDs, dates, and themes |
| `0x18` | `CREATE_BOARD` | Create and join a board |
| `0x19` | `DELETE_BOARD` | Delete a saved board with no connected clients |
| `0xFF` | `ERROR` | Return an error code and message |
Examples of JSON payloads:
```json
{"x":100,"y":120,"text":"Example card"}
```
This is a `CREATE_CARD` request. The server returns the created card with its assigned `cardID` and full state. Card positions use `x` and `y`; these values are the card center coordinates. Card image sources (local path or url) use the field name `mediaURL`.
```json
{"boardID":1}
```
This is a `SYNC_BOARD` request. The server responds with a board snapshot containing the board name and dates, theme, cards, connectors, and next object IDs.
```json
{"errCode":404,"errorMsg":"Card ID not found on this board."}
```
This is an `ERROR` payload. The opcode is `0xFF`. error codes and messages explain why a request failed.

## Data and logs
- The server creates or opens `opennote.db` in its working directory. This SQLite database stores saved boards, cards, connectors, themes, names, and dates.
- The server appends timestamped messages to `opennote.log` in its working directory and also writes them to the console.
- The log records server startup and listening port, client connections and disconnections, requests, outcomes, and errors. It should not be used to record passwords or other secrets.
- Run the server from the project directory if you want the database and log to stay there.

## Testing

Build with `make`, then start the server and client using the commands above. Test board selection and saving, card and connector operations, image file and URL uploads, and restoring a board after a server restart. Most test cases can be easily conducted through the use of the context menu (right click on background, card, or connector), or keyboard shortcuts.

Run the protocol test script after building with `python3 tests/protocol_runner.py`. It starts the server with a fresh database in a separate temporary folder. It saves test results, server console output, and the timestamped server log in that folder. Test results are produced by the script and are separate from server output. Use `--server`, `--port`, or `--output-dir` to set the server program, port, or output folder. IMPORTANT NOTE: This test script was written with the support of an AI chatbot ChatGPT unfortunately (Due to time crunch, my hand was forced). This test script was not fully generated, and the chatbot only served a supplemental role.

### Context Menu Options
| Where you right-click | Option | What it does |
|---|---|---|
| Empty board space | Create Card | Creates a card at the cursor |
| Empty board space | Edit theme... | Opens the theme editor |
| Empty board space | Save Board | Saves the current board |
| Empty board space | Save Board As... | Saves the board under a name you enter |
| Card | Connect to... | Starts connector mode; click another card to connect it |
| Card | Delete Card | Deletes that card |
| Connector | Delete Connector | Deletes that connector |
| Multiple selected objects | Delete Selected | Deletes the selected cards and connectors |

### Keybinds
| Key | What it does | When it applies |
|---|---|---|
| Delete | Deletes the selected cards and connectors | When a text field is not focused |
| Escape | Exits connector mode | While drawing a connector |
| Enter or keypad Enter | Confirms the selected color | In the color picker, when a text field is not focused |
| Enter | Saves the board name | While entering a name in “Save Board As...” |

## Current limitations

- The current client is a desktop GUI and needs a working GLFW/OpenGL display.
- Application does not function on windows.
- Authentication packets were removed due to complexity/times sake for P2.
- Image files and downloaded images are limited to 6 MiB and 64 × 1024 × 1024 pixels. HTTP and HTTPS image URL loading requires `curl` on each client machine that loads the image.
- Each card supports up to 4096 bytes of text.
- A complete board snapshot and each packet can be up to 10 MiB. A change that would make a board exceed the snapshot limit is rejected.
- The server accepts up to 16 client connections at a time.
- The server listens on `127.0.0.1` by default. External access requires an explicit listen address. Authentication and encrypted connections are not available, so use a trusted network.

## Third-party Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui) provides the GUI source in `imgui/`.
- [stb_image](https://github.com/nothings/stb) provides image decoding through `third_party/stb_image.h`.
- GLFW, OpenGL, SQLite, OpenSSL, and nlohmann/json are external build dependencies installed on the system.
