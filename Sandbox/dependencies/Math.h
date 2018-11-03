#pragma once

#include <iostream>
#include "GLFW\glfw3.h"
#ifndef DLL
#define DLL __declspec(dllexport)
#else 
#define DLL __declspec(dllimport)
#endif

class Vector3
{
	
public:
	DLL Vector3();
	DLL Vector3(float _x, float _y, float _z);
	DLL static Vector3 Up();
	DLL static Vector3 Down();
	DLL static Vector3 Left();
	DLL static Vector3 Right();
	DLL static Vector3 One();


	DLL Vector3& operator+(const Vector3& v);
	DLL Vector3& operator-(const Vector3& v);
	DLL Vector3& operator+=(const Vector3& v);
	DLL Vector3& operator-=(const Vector3& v);
	DLL Vector3& operator*(const float& x);
	DLL Vector3& operator*= (const float& x);
	DLL Vector3& operator/(const float& x);
	DLL Vector3& operator/=(const float& x);

	DLL bool& operator==(const Vector3& v);

	DLL float Magnitude();
	DLL Vector3 Normalized();

	float x, y, z;
};



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