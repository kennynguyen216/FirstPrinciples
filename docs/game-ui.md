# HTML game UI

[Documentation index](README.md) | [Project README](../README.md)

## Purpose

HTML pages provide game HUDs, menus and controls using familiar HTML, CSS and JavaScript. A bridge
lets scripts publish state and receive UI events without making gameplay depend on DOM details.

## Usage and behavior

Game UI is written in HTML, CSS and JavaScript and drawn by [Ultralight](https://ultralig.ht).
`Scripts/Setup.sh` fetches its SDK (or run `Scripts/GetUltralight.sh`, `.ps1` on Windows). Without
it everything still builds and UI simply does not draw.

Add a UI component and pick a page from `Content`, or drag an `.html` file onto an entity in the
viewport (onto empty space puts it on the primary camera). Like a Unity canvas, a UI on a camera
draws over that camera's view, and a UI on any other entity draws over every view. `Order` stacks
several pages and `Input` lets a page take the mouse and keyboard. Pages show while the game plays.

In the page, a global `engine` object talks to the game:

```html
Score: <b data-bind="score">0</b> <span id="result"></span>
<button onclick="engine.send('jump', { height: 2 })">Jump</button>
<script>
    engine.on("gameOver", function (data) { document.getElementById("result").textContent = data.won ? "You won" : "You lost"; });
    engine.watch("health", function (value) { document.body.classList.toggle("low", value < 20); });
</script>
```

- `data-bind="key"` elements show `engine.state[key]` and update by themselves.
- `engine.watch(key, fn)` runs whenever a script sets the key. `engine.on(name, fn)` hears events.
- `engine.send(name, data)` reaches scripts. Any JSON value works as data.
- `console.log` and page errors go to the Output Log. Saving the page, its CSS or its JS while
  playing reloads it, keeping the values scripts set.

In a C++ script on the same entity, or a scene script:

```cpp
void OnUpdate(float) override
{
    GetUI()->Set("score", Score);                 // numbers, bools and text
    if (Lives == 0)
        GetUI()->Emit("gameOver", "{\"won\":false}");
}

void OnUIEvent(std::string_view name, const Json& data) override
{
    if (name == "jump")
        Jump(data["height"].AsNumber(1));
}
```

`GetUI(entity)` reaches another entity's page. `RunScript(js)` runs JavaScript in the page and
`LoadPage(path)` swaps pages. `Json` reads what pages send: `data["player"]["name"].AsString()`. Add
> New UI Page... in the Content Browser writes a starter page, and double clicking pages, CSS and JS
opens them in a code tab. The 3D showcase camera has a HUD (`Content/UI/HUD.html` and
`Source/Scripts/ShowcaseHUD.cpp`) with live state, a Jump button, a speed slider and a shadow
switch.

Ultralight's free license covers commercial use for teams under US$200K in revenue and in funding.
The free edition caps pages at 60 frames per second, runs JavaScript without a JIT and has no video.
Pages render on the CPU and only changed frames upload to the GPU.

## How it works

UIRuntime manages Ultralight views and the JavaScript bridge. UIDocument exposes state, events and
page operations to C++, while UISystem updates scene-owned documents. Changed CPU-rendered pages are
uploaded as textures and composed over the appropriate camera view.

Source: [UIRuntime.h](../src/Engine/UI/UIRuntime.h), [UIDocument.h](../src/Engine/UI/UIDocument.h),
[UISystem.h](../src/Engine/UI/UISystem.h).

## Related guides

[C++ scripts](scripting.md), [Input and rebinding](input.md), [Page dependencies](assets.md).
