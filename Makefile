CXX = g++
CXXFLAGS = -g -std=c++20 -pthread -Wall -Wextra -Iimgui -Iimgui/backends
LDFLAGS = -pthread
CLIENT_LDFLAGS = -pthread -lGLEW -lglfw -lGL -ldl

# ImGui source files
IMGUI_SRCS = imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
             imgui/backends/imgui_impl_glfw.cpp imgui/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS = $(IMGUI_SRCS:.cpp=.o)

all: OpenNoteServer OpenNoteClient

# Build the Server exe
OpenNoteServer: OpenNoteServer.o
	$(CXX) $(CXXFLAGS) -o OpenNoteServer OpenNoteServer.o $(LDFLAGS)

# Build the client exe (client, renderer, and ImGui)
OpenNoteClient: OpenNoteClient.o ClientRenderer.o $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o OpenNoteClient OpenNoteClient.o ClientRenderer.o $(IMGUI_OBJS) $(CLIENT_LDFLAGS)

%.o: %.cpp BoardStructs.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# imgui core
imgui/%.o: imgui/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

imgui/backends/%.o: imgui/backends/%.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

 # clean
clean:
	rm -f OpenNoteServer OpenNoteClient *.o imgui/*.o imgui/backends/*.o core