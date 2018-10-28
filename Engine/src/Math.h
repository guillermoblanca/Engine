#pragma once

#include <iostream>

#define DLL __declspec(dllexport)

namespace GB {
	namespace Utils
	{
		DLL void  Print();
	}
}