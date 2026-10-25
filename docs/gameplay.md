# Worlds, components and systems

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

The World organizes a scene as entities with components and systems. Components hold editable data,
while systems update groups of matching entities. This lets game code add behavior without creating
a separate inheritance hierarchy for every object.

## Usage and behavior

Game code lives in your project's `Source` folder. The editor and the runtime both link it.

1. Components are plain structs. Fields can be `bool`, `int32`, `float`, `Vec2`, `Color` or `String`.

   ```cpp
   struct HealthComponent
   {
       int32 Value = 100;
   };
   ```

2. Register them in `Game::RegisterComponents` (`Source/Game.cpp`) so the Details panel and scene files see them.

   ```cpp
   registry.Register<HealthComponent>("Health")
       .Field("Value", &HealthComponent::Value);
   ```

3. Systems hold the logic. Subclass `FP::System`, loop over a view and add it in `Game::AddSystems`.

   ```cpp
   void Update(FP::World& world, float deltaTime) override
   {
       for (auto [entity, transform, health] : world.View<FP::TransformComponent, HealthComponent>().each())
       {
           if (health.Value <= 0)
               world.DestroyEntity(entity);
       }
   }
   ```

4. Input works the same in the editor and the runtime. Ask for actions like
   `FP::Input::WasActionPressed("Jump")` so keys and gamepads both work, or read keys directly with
   `FP::Input::IsKeyDown(SDL_SCANCODE_SPACE)`.

5. Scenes live in the project's `Content/Scenes`. Build them in the editor and press Play to run your systems.
   Pause holds the current game state and audio. Step advances one 1/60-second game frame while
   remaining paused. Resume continues play. Stop restores the scene.
   `Runtime` plays `Game::StartScene` or a scene passed on the command line.

Textures are `.png`, `.jpg`, `.tga` or `.bmp` files under `Content/`. Use the `...` button next to a
texture field to pick one.

## How it works

World wraps EnTT storage with stable entity IDs, names, creation order, parenting and play state.
Systems run in the order they were added while the world is playing. Component registration connects
plain C++ fields to the inspector and scene serializer. Entity destruction during play is deferred
until the update completes.

Source: [World.h](../src/Engine/World.h), [ComponentRegistry.h](../src/Engine/ComponentRegistry.h),
[SceneSerializer.h](../src/Engine/SceneSerializer.h).

## Related guides

[C++ scripts](scripting.md), [Input](input.md), [Hierarchy and transforms](hierarchy.md), [Object
lifetime](objects.md).
