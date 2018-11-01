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
	}
}