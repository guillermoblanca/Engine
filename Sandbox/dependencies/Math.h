#pragma once

#include <iostream>


#ifdef DLL
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
}