#pragma once

#include <iostream>
#ifndef DLL
#define DLL __declspec(dllexport)
#else 
#define DLL __declspec(dllimport)
#endif

namespace GB {
	namespace Utils
	{
		DLL void  Print();

		DLL void SecondTest();

		
	}

	namespace Core
	{

		//practice for game engine series 
		class Command 
		{
		public:
	
			 virtual void execute() =0;
		};
		class Example_Command : public Command
		{
		public:

			DLL  void execute();
		};





		class Window
		{

			char * title;
		public:

			DLL void CreateWindow(int w, int h, const char *title, const Command *context);
			DLL void SetTitle(const char * _title);
		};

		class Time
		{
			double deltaTime;
			double fixedTime;
			double renderTime;
		public:
			DLL double GetDeltaTime();

		};

		class Render
		{
		public:

			DLL void BeginRender();
			DLL void EndRender();

		};
	}
}