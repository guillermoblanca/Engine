#include "GB_GLWindow.h"

void GB_API CreateWindow(int w, int h, char * title)
{
	GLFWwindow * window = glfwCreateWindow(w, h, title, NULL, NULL);

	while (glfwWindowShouldClose(window))
	{

		//do something
	}
}
