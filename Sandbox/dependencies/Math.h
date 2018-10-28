#pragma once

#include <iostream>

#define DLL __declspec(dllimport)

namespace GB {
	namespace Utils
	{
		DLL void  Print();
	}
}