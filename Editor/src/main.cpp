// GB Editor. Options: --script=file | --exec="cmd; cmd" (run commands at startup), --quality=low|medium|high|ultra, --strict=1 (the exit code is 1 when a scripted line fails),
// plus the engine's --frames=N --screenshot=file.png --hidden --record=dir --fps=N. Type `help` in the console.
#include "EditorApp.h"
#include "GB/EntryPoint.h"

GB::Application* GB::CreateApplication()
{
	return new GB::Editor::EditorApp();
}
