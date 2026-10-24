#include "Tests/TestFramework.h"
#include "Core/CoreMinimal.h"
#include "Core/Memory/ArenaAllocator.h"

using namespace FP;

namespace
{
	// Counts live instances so tests can catch leaks and double destroys.
	struct Tracked
	{
		static inline int Live = 0;
		int Value = 0;
		Tracked(int value = 0) : Value(value) { ++Live; }
		Tracked(const Tracked& other) : Value(other.Value) { ++Live; }
		Tracked(Tracked&& other) noexcept : Value(other.Value) { ++Live; }
		Tracked& operator=(const Tracked&) = default;
		Tracked& operator=(Tracked&&) noexcept = default;
		~Tracked() { --Live; }
		bool operator==(const Tracked& other) const { return Value == other.Value; }
	};
}

// ---- Array -------------------------------------------------------------------

FP_TEST(Array_AddIndexAndRemove)
{
	Array<int> arr = { 1, 2, 3 };
	arr.Add(4);
	FP_EXPECT(arr.Num() == 4);
	FP_EXPECT(arr[3] == 4);
	arr.RemoveAt(0);
	FP_EXPECT(arr[0] == 2 && arr.Num() == 3);
	arr.RemoveAtSwap(0);
	FP_EXPECT(arr[0] == 4);
	arr.Insert(0, 9);
	FP_EXPECT(arr[0] == 9 && arr[1] == 4);
	FP_EXPECT(arr.Find(3) != INDEX_NONE);
	FP_EXPECT(arr.Remove(3) == 1);
	FP_EXPECT(!arr.Contains(3));
	FP_EXPECT(arr.Pop() == 4);
}

FP_TEST(Array_NonTrivialLifetimes)
{
	{
		Array<Tracked> arr;
		for (int i = 0; i < 100; ++i)
			arr.Emplace(i);
		FP_EXPECT(Tracked::Live == 100);
		Array<Tracked> copy = arr;
		FP_EXPECT(Tracked::Live == 200);
		Array<Tracked> moved = std::move(copy);
		FP_EXPECT(Tracked::Live == 200);
		arr.Resize(10);
		FP_EXPECT(Tracked::Live == 110);
		arr.RemoveAll([](const Tracked& t) { return t.Value % 2 == 0; });
		FP_EXPECT(arr.Num() == 5 && Tracked::Live == 105);
	}
	FP_EXPECT(Tracked::Live == 0);
}

FP_TEST(Array_EmplaceFromOwnElementWhileGrowing)
{
	Array<String> arr;
	arr.Add("a long string that does not fit inline storage");
	while (arr.Num() < arr.Capacity())
		arr.Add("x");
	arr.Add(arr[0]);
	FP_EXPECT(arr.Last() == arr[0]);
}

FP_TEST(Array_UsesExtraSpaceFromAllocator)
{
	ArenaAllocator arena(1024 * 1024);
	Array<int> arr(arena);
	for (int i = 0; i < 1000; ++i)
		arr.Add(i);
	FP_EXPECT(arr.Num() == 1000);
	FP_EXPECT(arr[999] == 999);
}

FP_TEST(Array_Sort)
{
	Array<int> arr = { 5, 1, 4 };
	arr.Sort();
	FP_EXPECT(arr == Array<int>({ 1, 4, 5 }));
}

// ---- HashMap -----------------------------------------------------------------

FP_TEST(HashMap_AddFindRemove)
{
	HashMap<int, String> map;
	for (int i = 0; i < 1000; ++i)
		map.Add(i, String::Format("v%d", i));
	FP_EXPECT(map.Num() == 1000);
	FP_EXPECT(*map.Find(500) == "v500");
	FP_EXPECT(map.Find(5000) == nullptr);

	for (int i = 0; i < 1000; i += 2)
		FP_EXPECT(map.Remove(i));
	FP_EXPECT(map.Num() == 500);
	for (int i = 0; i < 1000; ++i)
		FP_EXPECT(map.Contains(i) == (i % 2 == 1));

	int count = 0;
	for (auto& pair : map)
	{
		FP_EXPECT(pair.Value == String::Format("v%d", pair.Key));
		++count;
	}
	FP_EXPECT(count == 500);
}

FP_TEST(HashMap_StringKeysAndOverwrite)
{
	HashMap<String, int> map;
	map["one"] = 1;
	map["two"] = 2;
	map.Add("one", 11);
	FP_EXPECT(map.Num() == 2);
	FP_EXPECT(map.FindRef("one") == 11);
	FP_EXPECT(map.FindRef("three", -1) == -1);
}

FP_TEST(HashMap_ValueLifetimes)
{
	{
		HashMap<int, Tracked> map;
		for (int i = 0; i < 200; ++i)
			map.Add(i, Tracked(i));
		for (int i = 0; i < 100; ++i)
			map.Remove(i);
		HashMap<int, Tracked> copy = map;
		FP_EXPECT(Tracked::Live == 200);
	}
	FP_EXPECT(Tracked::Live == 0);
}

// ---- String ------------------------------------------------------------------

FP_TEST(String_InlineAndHeap)
{
	String small = "hello";
	FP_EXPECT(small.Length() == 5 && small == "hello");
	String big = small;
	for (int i = 0; i < 10; ++i)
		big += " world";
	FP_EXPECT(big.Length() == 65);
	FP_EXPECT(big.StartsWith("hello world"));
	String moved = std::move(big);
	FP_EXPECT(moved.Length() == 65 && big.IsEmpty());
	moved.Append(moved.View());
	FP_EXPECT(moved.Length() == 130);
}

FP_TEST(String_Operations)
{
	String s = String::Format("%s-%d", "item", 42);
	FP_EXPECT(s == "item-42");
	FP_EXPECT(s.Find("42") == 5);
	FP_EXPECT(s.Substring(0, 4) == "item");
	FP_EXPECT(s.Right(2) == "42");
	FP_EXPECT(String("  pad ").Trimmed() == "pad");
	FP_EXPECT(String("MiXeD").ToLower() == "mixed");
	FP_EXPECT(String("abc").EqualsIgnoreCase("ABC"));

	Array<String> parts = String("a,b,,c").Split(',');
	FP_EXPECT(parts.Num() == 3 && parts[2] == "c");

	String r = "a.b.c";
	FP_EXPECT(r.Replace(".", "::") == 2);
	FP_EXPECT(r == "a::b::c");

	int64 i = 0;
	double f = 0;
	FP_EXPECT(String("123").ToInt(i) && i == 123);
	FP_EXPECT(!String("12x").ToInt(i));
	FP_EXPECT(String("1.5").ToFloat(f) && f == 1.5);
}

// ---- RefPtr ------------------------------------------------------------------

namespace
{
	struct Resource : RefCounted
	{
		static inline int Alive = 0;
		Resource() { ++Alive; }
		~Resource() override { --Alive; }
	};

	struct DerivedResource : Resource
	{
	};
}

FP_TEST(RefPtr_CountsAndDeletes)
{
	{
		RefPtr<Resource> a = MakeRef<Resource>();
		FP_EXPECT(a->GetRefCount() == 1);
		RefPtr<Resource> b = a;
		FP_EXPECT(a->GetRefCount() == 2);
		Resource* raw = a.Get();
		RefPtr<Resource> c(raw); // intrusive count lets raw pointers become owners again
		FP_EXPECT(raw->GetRefCount() == 3);
		b.Reset();
		FP_EXPECT(raw->GetRefCount() == 2);
		RefPtr<Resource> base = MakeRef<DerivedResource>();
		FP_EXPECT(Resource::Alive == 2);
	}
	FP_EXPECT(Resource::Alive == 0);
}

// ---- Guid --------------------------------------------------------------------

FP_TEST(Guid_NewParseRoundTrip)
{
	Guid a = Guid::New();
	Guid b = Guid::New();
	FP_EXPECT(a.IsValid() && a != b);

	String text = a.ToString();
	FP_EXPECT(text.Length() == 36);
	FP_EXPECT(text[14] == '4');

	Guid parsed;
	FP_EXPECT(Guid::Parse(text, parsed));
	FP_EXPECT(parsed == a);
	FP_EXPECT(!Guid::Parse("not-a-guid", parsed));

	HashMap<Guid, int> map;
	map.Add(a, 1);
	FP_EXPECT(map.FindRef(parsed) == 1);
}

#include "Core/Misc/File.h"

#include <filesystem>
#include <string>
#include <vector>

namespace
{
	struct CapturedLine
	{
		FP::LogLevel Level;
		std::string Text;
	};

	void CaptureSink(FP::LogLevel level, const char* message, void* userData)
	{
		static_cast<std::vector<CapturedLine>*>(userData)->push_back({ level, message });
	}
}

FP_TEST(Log_WritesToSinksAndFiles)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path folder = fs::temp_directory_path(ec) / "FPLogTest";
	fs::remove_all(folder, ec);
	const std::string path = (folder / "Logs" / "Test.log").generic_string();

	std::vector<CapturedLine> lines;
	FP_EXPECT(Log::OpenFile(path.c_str()));
	Log::AddSink(&CaptureSink, &lines);
	FP_LOG_WARN("disk %d%% full", 93);
	// Longer than the stack buffer, so it takes the slow path and must come out whole.
	const std::string longText(3000, 'x');
	FP_LOG("%s|end", longText.c_str());
	Log::RemoveSink(&CaptureSink, &lines);
	FP_LOG("not captured");
	Log::Flush();

	FP_EXPECT(lines.size() == 2);
	if (lines.size() == 2)
	{
		// Sinks get a timestamp and the level tag, but no newline.
		FP_EXPECT(lines[0].Level == LogLevel::Warning);
		FP_EXPECT(lines[0].Text.find("[warning] disk 93% full") != std::string::npos);
		FP_EXPECT(lines[0].Text[0] == '[' && lines[0].Text.back() != '\n');
		FP_EXPECT(lines[1].Text.find(longText + "|end") != std::string::npos);
	}

	// The file has every line with its level spelled out.
	String text;
	FP_EXPECT(File::ReadAllText(path.c_str(), text));
	FP_EXPECT(text.Contains("[warning] disk 93% full"));
	FP_EXPECT(text.Contains("[info] not captured"));
	FP_EXPECT(text.Contains("|end"));
}
