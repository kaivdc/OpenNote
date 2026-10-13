#ifndef CLIENT_RENDERER_H
#define CLIENT_RENDERER_H

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "BoardStructs.h"

GLFWwindow* initRenderer(int width, int height, const char* title);
void cleanupRenderer(GLFWwindow* window);
void renderFrame(GLFWwindow* window, BoardState& localBoard);

#endif