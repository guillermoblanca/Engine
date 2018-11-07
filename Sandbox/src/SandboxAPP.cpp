#include "GB.h"

class Sandbox : public GB::Application
{
public:

	Sandbox()
	{

	}

	~Sandbox()
	{

	}

};


GB::Application * GB::CreateApplication()
{
	return new Sandbox();
}