#include "GB.h"

class Sandbox : public GB::Application
{
public:

	Sandbox()
	{
		GB::CreateApplication();
	}

	~Sandbox()
	{

	}

};


GB::Application * GB::CreateApplication()
{
	return new Sandbox();
}