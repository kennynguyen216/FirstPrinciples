// The ShowcaseHUD script on the camera sets state and speed, and hears what is sent here.

// The badge takes the state's name as a class so each state gets its own color.
engine.watch("state", function (state) {
	document.querySelector(".badge").className = "badge " + state;
});

document.getElementById("jump").addEventListener("click", function () {
	engine.send("jump");
});

var speed = document.getElementById("animationSpeed");
speed.addEventListener("input", function () {
	document.getElementById("animationSpeedValue").textContent = Number(speed.value).toFixed(2) + "x";
	engine.send("animationSpeed", Number(speed.value));
});

document.getElementById("shadows").addEventListener("change", function (event) {
	engine.send("shadows", event.target.checked);
});

// Counted in the page itself, since it needs nothing from the game.
var started = Date.now();
setInterval(function () {
	var seconds = Math.floor((Date.now() - started) / 1000);
	document.getElementById("clock").textContent = Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0");
}, 250);
