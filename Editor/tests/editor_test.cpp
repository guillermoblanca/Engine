// Editor logic without a window: property registry, command runner and the log buffer. CPU only.
#include "Commands.h"
#include "LogBuffer.h"
#include "Properties.h"
#include <cstdio>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}
	enum class Mode : int { A, B, C };
}

int main()
{
	using namespace GB::Editor;

	float f = 0.5f; int i = 3; bool b = false; glm::vec3 v(1.0f), c(0.0f); Mode m = Mode::A;
	int changes = 0;
	PropertyRegistry reg;
	reg.Float("a.f", f, 0.0f, 1.0f, "A").OnChange = [&] { ++changes; };
	reg.Int("a.i", i, 0, 10, "A");
	reg.Bool("a.b", b, "A");
	reg.Vec3("a.v", v, -5.0f, 5.0f, "A");
	reg.Color("a.c", c, "A");
	reg.Enum("a.m", m, { "A", "B", "C" }, "A");

	Check(reg.Set("a.f", "0.25").Ok && f == 0.25f, "float set");
	Check(changes == 1, "onchange fired");
	Check(reg.Set("a.f", "7").Ok && f == 1.0f, "float clamped");
	Check(!reg.Set("a.f", "abc").Ok, "bad float rejected");
	Check(!reg.Set("nope", "1").Ok, "unknown path rejected");
	Check(reg.Set("a.i", "99").Ok && i == 10, "int clamped");
	Check(reg.Set("a.b", "true").Ok && b, "bool set");
	Check(reg.Set("a.v", "1 2 3").Ok && v == glm::vec3(1, 2, 3), "vec3 set");
	Check(reg.Set("a.c", "#ff0000").Ok && c.r > 0.99f && c.g < 0.01f, "color hex");
	Check(reg.Set("a.m", "C").Ok && m == Mode::C, "enum by name");
	Check(reg.Set("a.m", "1").Ok && m == Mode::B, "enum by index");
	Check(!reg.Set("a.m", "Z").Ok, "bad enum rejected");
	Check(reg.List("a.").size() == 6, "list by prefix");
	Check(!reg.Get("a.i").empty() && reg.Get("zzz").empty(), "get");
	reg.RemovePrefix("a.");
	Check(reg.All().empty(), "remove prefix");

	Check(CommandRunner::Tokenize("set \"a b\" 3 # note").size() == 3, "tokenize quotes and comments");
	Check(CommandRunner::SplitScript("a; b\nc # x\n\n").size() == 3, "split script");

	CommandRunner cmd;
	int sum = 0;
	cmd.Register("add", "adds", [&](const std::vector<std::string>& a) { sum += std::stoi(a.at(0)); return CommandResult{}; });
	Check(cmd.Execute("add 4").Ok && sum == 4, "command runs");
	Check(!cmd.Execute("bogus").Ok, "unknown command");
	Check(cmd.Complete("a").size() == 1, "completion");

	LogBuffer buf;
	buf.Add(spdlog::level::info, "hola");
	Check(buf.Snapshot().size() == 1, "log buffer");
	buf.Clear();
	Check(buf.Snapshot().empty(), "log clear");

	if (failures) return 1;
	std::puts("editor tests passed");
	return 0;
}
