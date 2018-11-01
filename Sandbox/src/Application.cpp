
#include <iostream>
#include "../dependencies/Math.h"


int main()
{
	GB::Utils::Print();
	GB::Core::Example_Command command;

	Vector3 vector1,vector2;
	
	vector1.x = 2;
	vector2.x = 3;

	vector1 = vector1 + vector2;
	std::cout << vector1.x << std::endl;
	command.execute();
	std::cin.get();
	return 0;
}