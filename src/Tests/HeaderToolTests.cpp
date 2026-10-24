#include "Tests/TestFramework.h"
#include "Tools/HeaderTool/HeaderParser.h"

using namespace FP::HeaderTool;

namespace
{
	const char* const Source = R"cpp(
#include "Engine/Scripting/Script.h"
#define NOT_A_CLASS class Fake {

namespace Game
{
	// Chases the player.
	// Gives up when far away.
	FPCLASS(Category = "Enemies")
	class Chaser : public FP::Script
	{
	public:
		FPPROPERTY(Range = "0, 20", Category = "Movement")
		float Speed = 4; // meters per second

		// Who to chase.
		FPPROPERTY(Asset = "Scenes (*.scene)")
		FP::String Target = "Player";

		FPPROPERTY(ReadOnly, Tooltip = "Set while chasing, \"live\"")
		bool Chasing = false;

		FPPROPERTY()
		FP::Array<int> Path{ 1, 2 };

		float NotExposed = 1;

		void OnStart() override { OnUpdate(0); }
		void OnUpdate(float) override {}

		/* Turns around. */
		FPFUNCTION()
		void Reverse() { Speed = -Speed; }

	private:
		enum class Mode { Idle, Run };
		int m_Hidden = 0;
	};
}

struct Plain : FP::Script
{
	FPPROPERTY() int Count = 0;
};

class NotAScript { int x; };
)cpp";
}

FP_TEST(HeaderTool_ReadsMarkersAndComments)
{
	std::vector<Class> classes;
	std::vector<Error> errors;
	ParseFile(Source, "Chaser.cpp", classes, errors);
	FP_EXPECT(errors.empty());
	FP_EXPECT(classes.size() == 2);
	if (classes.size() != 2)
		return;

	const Class& chaser = classes[0];
	FP_EXPECT(chaser.Name == "Chaser" && chaser.Namespace == "Game" && !chaser.IsStruct);
	FP_EXPECT(chaser.Description == "Chases the player. Gives up when far away.");
	FP_EXPECT(chaser.Category == "Enemies");
	FP_EXPECT(chaser.Callbacks.size() == 2 && chaser.Callbacks[0] == "OnStart" && chaser.Callbacks[1] == "OnUpdate");
	FP_EXPECT(chaser.Properties.size() == 4);
	FP_EXPECT(chaser.Properties[0].Name == "Speed" && chaser.Properties[0].HasRange && chaser.Properties[0].Max == 20);
	FP_EXPECT(chaser.Properties[0].Tooltip == "meters per second" && chaser.Properties[0].Category == "Movement");
	FP_EXPECT(chaser.Properties[1].Name == "Target" && chaser.Properties[1].Tooltip == "Who to chase." && chaser.Properties[1].Asset == "Scenes (*.scene)");
	FP_EXPECT(chaser.Properties[2].ReadOnly && chaser.Properties[2].Tooltip == "Set while chasing, \"live\"");
	FP_EXPECT(chaser.Properties[3].Name == "Path");
	FP_EXPECT(chaser.Functions.size() == 1 && chaser.Functions[0].Name == "Reverse");

	// Structs derived from Script are picked up even without FPCLASS.
	FP_EXPECT(classes[1].Name == "Plain" && classes[1].IsStruct && classes[1].Properties.size() == 1);

	// The output names everything through T so it compiles before the class is defined.
	const std::string header = Generate(classes, "Scripts");
	FP_EXPECT(header.find("namespace Game { class Chaser; }") != std::string::npos);
	FP_EXPECT(header.find("struct FP::ScriptReflection<::Game::Chaser>") != std::string::npos);
	FP_EXPECT(header.find("fields.Field(\"Speed\", &T::Speed).Range(0.0f, 20.0f).Category(\"Movement\")") == std::string::npos);
	FP_EXPECT(header.find("fields.Field(\"Speed\", &T::Speed).Tooltip(\"meters per second\").Category(\"Movement\").Range(0.0f, 20.0f);") != std::string::npos);
	FP_EXPECT(header.find("Tooltip(\"Set while chasing, \\\"live\\\"\").ReadOnly()") != std::string::npos);
	FP_EXPECT(header.find("fields.template Function<&T::Reverse>(\"Reverse\", nullptr);") != std::string::npos);
	FP_EXPECT(Generate(classes, "Scripts") == header);
}

FP_TEST(HeaderTool_ReportsMistakes)
{
	std::vector<Class> classes;
	std::vector<Error> errors;
	ParseFile(R"cpp(
class Hidden : public Script
{
	FPPROPERTY()
	float Secret = 1;
public:
	FPFUNCTION()
	void Take(int amount) {}
};
FPPROPERTY() float Loose = 0;
class Friendly : public Script
{
	FP_BODY()
	FPPROPERTY(Range = "nope")
	float Fine = 1;
};
)cpp", "Bad.cpp", classes, errors);
	FP_EXPECT(errors.size() == 4);
	if (errors.size() != 4)
		return;
	FP_EXPECT(errors[0].Line == 4 && errors[0].Message.find("not public") != std::string::npos);
	FP_EXPECT(errors[1].Line == 7 && errors[1].Message.find("no arguments") != std::string::npos);
	FP_EXPECT(errors[2].Line == 10 && errors[2].Message.find("inside a script class") != std::string::npos);
	FP_EXPECT(errors[3].Line == 14 && errors[3].Message.find("two numbers") != std::string::npos && errors[3].File == "Bad.cpp");
	// FP_BODY lets private members through.
	FP_EXPECT(classes.size() == 2 && classes[1].Properties.size() == 1);
}
