// The life cycle of GBObject (GBObject.h): a name and a place with no model, phases in order, components started in the order they were
// added and ended in the reverse one, EndPlay before OnDestroy, pause and resume once, calls out of order ignored. CPU only.
#include "GB/Game/GBObject.h"
#include <cstdio>
#include <string>

namespace
{
	int failures = 0;
	void Check(bool ok, const char* what)
	{
		if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what); ++failures; }
	}

	std::string g_Log;

	struct Probe : GB::GBObjectComponent
	{
		explicit Probe(char id) : Id(id) {}
		~Probe() override { g_Log += std::string("~") + Id; }
		void OnSpawnStart(GB::GBObject&, GB::GBObjectContext&) override { g_Log += std::string("S") + Id; }
		void OnSpawnEnd(GB::GBObject&) override { g_Log += std::string("E") + Id; }
		void BeginPlay(GB::GBObject&) override { g_Log += std::string("B") + Id; }
		void Update(GB::GBObject&, float) override { g_Log += std::string("U") + Id; }
		void OnPause(GB::GBObject&) override { g_Log += std::string("P") + Id; }
		void OnResume(GB::GBObject&) override { g_Log += std::string("R") + Id; }
		void EndPlay(GB::GBObject&) override { g_Log += std::string("e") + Id; }
		void OnDestroy(GB::GBObject&) override { g_Log += std::string("d") + Id; }
		char Id;
	};
}

int main()
{
	{
		GB::GBObject o("marker");
		Check(o.Name() == "marker" && o.State() == GB::ObjectState::Constructing, "a bare object has a name and is constructing");
		o.Transform.Position = { 1, 2, 3 };
		GB::GBObjectContext ctx;
		o.Update(0.1f);
		o.BeginPlay();
		Check(o.State() == GB::ObjectState::Constructing, "BeginPlay before the spawn phases is ignored");
		o.OnSpawnStart(ctx);
		o.OnSpawnEnd();
		o.BeginPlay();
		Check(o.State() == GB::ObjectState::Active && o.Alive(), "an object with no components goes through every phase");
		o.OnPause();
		Check(o.State() == GB::ObjectState::Paused, "paused");
		o.OnDestroy();
		Check(o.State() == GB::ObjectState::Destroyed && !o.Alive(), "destroy ends it first");
		o.OnDestroy();   // idempotent
	}
	{
		g_Log.clear();
		auto o = std::make_unique<GB::GBObject>("a");
		o->AddComponent<Probe>('1');
		o->AddComponent<Probe>('2');
		GB::GBObjectContext ctx;
		o->OnSpawnStart(ctx);
		o->OnSpawnStart(ctx);   // twice: once
		o->OnSpawnEnd();
		o->BeginPlay();
		o->BeginPlay();
		o->Update(0.1f);
		o->OnPause();
		o->OnPause();
		o->Update(0.1f);        // paused: no tick
		o->OnResume();
		o->OnResume();
		o->OnDestroy();
		Check(g_Log == "S1S2" "E1E2" "B1B2" "U1U2" "P1P2" "R1R2" "e2e1" "d2d1" "~2~1", ("phase order: " + g_Log).c_str());
		Check(o->Find<Probe>() == nullptr, "components are released after OnDestroy");
	}
	{
		g_Log.clear();
		auto o = std::make_unique<GB::GBObject>("b");
		o->AddComponent<Probe>('1');
		GB::GBObjectContext ctx;
		o->OnSpawnStart(ctx);
		o->OnSpawnEnd();
		o->BeginPlay();
		o->EndPlay();
		o->EndPlay();
		o->Update(0.1f);
		Check(o->State() == GB::ObjectState::Ending && g_Log == "S1E1B1e1", ("ended once and does not tick: " + g_Log).c_str());
		o.reset();   // the destructor releases the components; it does not emit phases
		Check(g_Log == "S1E1B1e1~1", ("the destructor only frees: " + g_Log).c_str());
	}
	if (failures == 0) std::puts("gbobject: ok");
	return failures == 0 ? 0 : 1;
}
