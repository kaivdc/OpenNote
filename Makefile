CXX ?= g++

# Detect the host OS
UNAME_S := $(shell uname -s)

# Common compile flags
CXXFLAGS = -g -std=c++20 -pthread -Wall -Wextra -Iclient -Iserver -Ishared -Iimgui -Iimgui/backends -Ithird_party

# Base link flags, extended per-platform below
SERVER_LDFLAGS = -pthread
CLIENT_LDFLAGS = -pthread

SERVER_LDFLAGS += -lsqlite3

ifeq ($(UNAME_S),Darwin)
    # Homebrew prefix differs between Intel (/usr/local) and Apple Silicon (/opt/homebrew)
    BREW_PREFIX := $(shell brew --prefix 2>/dev/null)
    ifneq ($(BREW_PREFIX),)
        CXXFLAGS       += -I$(BREW_PREFIX)/include -I$(BREW_PREFIX)/opt/openssl@3/include -I$(BREW_PREFIX)/opt/nlohmann-json/include
        SERVER_LDFLAGS += -L$(BREW_PREFIX)/opt/openssl@3/lib -lcrypto
        CLIENT_LDFLAGS += -L$(BREW_PREFIX)/lib -L$(BREW_PREFIX)/opt/openssl@3/lib -lcrypto
    endif
    # OpenGL is deprecated
    CXXFLAGS       += -DGL_SILENCE_DEPRECATION
    CLIENT_LDFLAGS += -lglfw -framework OpenGL -framework Cocoa -framework IOKit -framework CoreVideo
else ifeq ($(UNAME_S),Linux)
    # Linux & WSL
    SERVER_LDFLAGS += -lcrypto
    CLIENT_LDFLAGS += -lglfw -lGL -ldl -lcrypto
# Default to linux make so we can expand if we choose to add windows while keeping linux defined above
else
    SERVER_LDFLAGS += -lcrypto
    CLIENT_LDFLAGS += -lglfw -lGL -ldl -lcrypto
endif

# ImGui source files
IMGUI_SRCS = imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
             imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

SERVER_OBJS = server/OpenNoteServer.o server/ServerBoardCommandProcessor.o server/BoardDatabase.o server/ServerLogging.o
CLIENT_OBJS = client/OpenNoteClient.o client/ClientRenderer.o client/BoardInspector.o client/ClientImageCache.o \
              client/LocalBoardCommandHandler.o client/PacketCreation.o

all: OpenNoteServer OpenNoteClient

# Build the Server exe
OpenNoteServer: $(SERVER_OBJS)
	$(CXX) $(CXXFLAGS) -o OpenNoteServer $(SERVER_OBJS) $(SERVER_LDFLAGS)

# Build the client exe (client, renderer, and ImGui)
OpenNoteClient: $(CLIENT_OBJS) $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o OpenNoteClient $(CLIENT_OBJS) $(IMGUI_OBJS) $(CLIENT_LDFLAGS)

$(CLIENT_OBJS): shared/BoardStructs.h shared/ClientSettings.h client/BoardCommands.h client/BoardUIState.h \
                client/BoardInspector.h client/ClientImageCache.h client/ClientRenderer.h \
                client/LocalBoardCommandHandler.h client/PacketCreation.h
$(SERVER_OBJS): shared/BoardStructs.h shared/ClientSettings.h server/ServerBoardCommandProcessor.h \
                server/BoardDatabase.h server/ServerLogging.h

client/%.o: client/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

server/%.o: server/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# imgui core
imgui/%.o: imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui/backends/%.o: imgui/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

 # clean
clean:
	rm -f OpenNoteServer OpenNoteClient *.o client/*.o server/*.o imgui/*.o imgui/backends/*.o core
