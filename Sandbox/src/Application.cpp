
#include <iostream>
#include "../dependencies/Math.h"


int main()
{
	GB::Utils::Print();

	GB::Core::Example_Command command;
	command.execute();
	std::cin.get();
	return 0;
}