#include "Tests/TestFramework.h"
#include "CoreObject/CoreObject.h"

using namespace FP;

namespace
{
	class TestNode : public Object
	{
		FP_OBJECT(TestNode, Object)

	public:
		static inline int Destroyed = 0;

		TestNode* Child = nullptr;
		Array<TestNode*> Links;
		int Value = 0;

		void AddReferencedObjects(ReferenceCollector& collector) override
		{
			Super::AddReferencedObjects(collector);
			collector.Add(Child);
			collector.Add(Links);
		}

		void OnDestroy() override { ++Destroyed; }
	};

	class SpecialNode : public TestNode
	{
		FP_OBJECT(SpecialNode, TestNode)
	};
}

FP_TEST(Object_ClassInfo)
{
	FP_EXPECT(Class::Find("TestNode") == TestNode::StaticClass());
	FP_EXPECT(SpecialNode::StaticClass()->IsChildOf(TestNode::StaticClass()));
	FP_EXPECT(!TestNode::StaticClass()->IsChildOf(SpecialNode::StaticClass()));

	SpecialNode* node = NewObject<SpecialNode>();
	FP_EXPECT(node->IsA<TestNode>());
	FP_EXPECT(Cast<SpecialNode>(static_cast<Object*>(node)) == node);
	FP_EXPECT(node->GetGuid().IsValid());
	FP_EXPECT(node->GetName().StartsWith("SpecialNode_"));

	Object* created = NewObject(Class::Find("SpecialNode"));
	FP_EXPECT(Cast<SpecialNode>(created) != nullptr);
	GarbageCollector::Get().Collect();
}

FP_TEST(GC_CollectsUnreachable)
{
	GarbageCollector& gc = GarbageCollector::Get();
	gc.Collect();
	const int32 before = gc.GetNumObjects();
	TestNode::Destroyed = 0;

	TestNode* root = NewObject<TestNode>();
	root->AddToRoot();
	root->Child = NewObject<TestNode>();
	root->Child->Child = NewObject<TestNode>();
	root->Links.Add(NewObject<TestNode>());
	NewObject<TestNode>(); // garbage
	NewObject<TestNode>(); // garbage

	GarbageCollectionStats stats = gc.Collect();
	FP_EXPECT(stats.NumFreed == 2);
	FP_EXPECT(TestNode::Destroyed == 2);
	FP_EXPECT(gc.GetNumObjects() == before + 4);

	root->RemoveFromRoot();
	stats = gc.Collect();
	FP_EXPECT(stats.NumFreed == 4);
	FP_EXPECT(gc.GetNumObjects() == before);
}

FP_TEST(GC_HandlesCycles)
{
	GarbageCollector& gc = GarbageCollector::Get();
	gc.Collect();
	TestNode* a = NewObject<TestNode>();
	TestNode* b = NewObject<TestNode>();
	a->Child = b;
	b->Child = a;
	FP_EXPECT(gc.Collect().NumFreed == 2);
}

FP_TEST(GC_OuterKeepsOwnerAlive)
{
	GarbageCollector& gc = GarbageCollector::Get();
	gc.Collect();
	TestNode* owner = NewObject<TestNode>();
	StrongObjectPtr<TestNode> inner(NewObject<TestNode>(owner));
	FP_EXPECT(gc.Collect().NumFreed == 0);
	FP_EXPECT(inner->GetTypedOuter<TestNode>() == owner);
	inner.Reset();
	FP_EXPECT(gc.Collect().NumFreed == 2);
}

FP_TEST(GC_DestroyNullsReferences)
{
	GarbageCollector& gc = GarbageCollector::Get();
	StrongObjectPtr<TestNode> root(NewObject<TestNode>());
	TestNode* child = NewObject<TestNode>();
	root->Child = child;
	root->Links.Add(child);

	WeakObjectPtr<TestNode> weak = child;
	FP_EXPECT(weak.Get() == child);

	child->Destroy();
	FP_EXPECT(!weak);
	gc.Collect();
	FP_EXPECT(root->Child == nullptr);
	FP_EXPECT(root->Links.IsEmpty());

	// The slot gets reused but the old handle stays dead.
	TestNode* reused = NewObject<TestNode>();
	FP_EXPECT(weak.Get() == nullptr);
	(void)reused;
}

FP_TEST(GC_StressManyObjects)
{
	GarbageCollector& gc = GarbageCollector::Get();
	gc.Collect();
	const int32 before = gc.GetNumObjects();
	StrongObjectPtr<TestNode> root(NewObject<TestNode>());
	for (int i = 0; i < 20000; ++i)
	{
		TestNode* node = NewObject<TestNode>();
		node->Value = i;
		if (i % 2 == 0)
			root->Links.Add(node);
	}
	GarbageCollectionStats stats = gc.Collect();
	FP_EXPECT(stats.NumFreed == 10000);
	FP_EXPECT(gc.GetNumObjects() == before + 10001);
	root.Reset();
	gc.Collect();
	FP_EXPECT(gc.GetNumObjects() == before);
}
