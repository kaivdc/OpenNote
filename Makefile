CXX = g++
CXXFLAGS = -g -std=c++11 -pthread -Wall -Wextra
LDFLAGS = -pthread

# Targets
all: OpenNoteServer OpenNoteClient

# Build the Server exe
OpenNoteServer: OpenNoteServer.o
	$(CXX) $(CXXFLAGS) -o OpenNoteServer OpenNoteServer.o $(LDFLAGS)

# Build the client exe (with renderer and client)
OpenNoteClient: OpenNoteClient.o ClientRenderer.o
	$(CXX) $(CXXFLAGS) -o OpenNoteClient OpenNoteClient.o ClientRenderer.o $(LDFLAGS)

%.o: %.cpp BoardStructs.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean
clean:
	rm -f OpenNoteServer OpenNoteClient *.o core