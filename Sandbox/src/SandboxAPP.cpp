#include "GB.h"

class Sandbox : public GB::Application
{
};

GB::Application* GB::CreateApplication()
{
	return new Sandbox();
}
