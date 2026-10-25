
Proposals for giving the Driver meaningful, fun reasons to leave the wheel. Written against the Driver spec in the Dead Air Freight design doc (Sections 4–6).

## The problem

- **The scoring has no real tradeoff.** Score is cargo delivered plus a time bonus. Oxygen only drains over time (breathing) and through tickets. So the fastest truck also breathes the least, and it wins on both parts of the score. "Arrive faster vs. carry more oxygen" is not a real choice yet.
- **There are only two reasons to leave the wheel, and one isn't fun.** Scratch tickets are fun. The scrubber repair (hold E for 4 seconds) is a chore.
- **The doc promises two things the mechanics don't deliver:**
  - **Power management.** Section 2 says "Oxygen, CO2, and power are managed every second," but power is only a HUD number that flickers on C-class flares.
  - **A response to flare warnings.** The 5-second warning is "time to react," but there is nothing to react *with*.

## How each task is judged

Every task that takes the Driver away from the wheel must:

1. **Solve a problem steering can't.**
2. **Make timing a real choice:** do it now, or risk waiting.
3. **Be a short skill, not holding a key.**
4. **Be based on real spaceflight**, the same way the weather data is real.

## Open dependency

Flare data was removed from the Driver, and the scrubber currently never breaks. Proposals 1 and 2 both need something that breaks the scrubber. They can bring flares back as the cause, or use another cause (asteroid hits, high Kp).

## Proposals (priority order)

### 1. Scrubber canister swap (replaces hold-E repair)

The Driver grabs a fresh lithium hydroxide (LiOH) canister from a rack, carries it across the cab, and slots it into the scrubber.

- **Why it fits:** Apollo 13. The crew had to rig LiOH canisters to survive.
- **Why it's fun:** it uses the on-foot cab, so the cab layout becomes part of the challenge. It's an active trip instead of a progress bar.
- **Optional scarcity:** a limited number of spare canisters adds a small resource to manage.
- **Scope:** small. One pickup, one carry state, and one slot interaction, replacing the hold timer.

### 2. Flare shutters (gives the warning a purpose)

When a flare warning goes off, the Driver can run to a panel and close the radiation shutters before the hit.

- **Effect:** with the shutters closed, an M-class flare doesn't break the scrubber, and an X-class flare's wobble burst is reduced.
- **The tradeoff:** flare warnings usually land during a storm, so this is the design doc's "money moment": protect the scrubber, or keep control of the truck.
- **Why it fits:** shielding from solar radiation is a real crew procedure.
- **Scope:** cheap. One interactable object, a "shutters closed" flag, and one check in the flare-hit code.
- **If only one gets built, build this one.** It has the best value for the least work.

### 3. Oxygen generator plus a shared power pool (creates the speed vs. oxygen tradeoff)

A station that turns water into oxygen (electrolysis, as on the ISS) and refills the cab reserve, so the crew breathes less of the cargo.

- **Power becomes a real resource:**
  - Thrust draws power.
  - The generator draws power.
  - Power recharges slowly.
- **The tradeoff:** floor it, or ease off and step away to make air. That gives a real racer vs. hauler split.
- **The minigame:** keep a pressure needle in the green while it runs. It's short and active, and you can fail it.
- **Scope:** the largest of the three. Build it only if 1 and 2 aren't enough. Expect to retune the time bonus so neither playstyle always wins.

## Deliberately skipped

- **Radar or scanner station.** It turns the game into reading information instead of wrestling the truck, and asteroids are already visible.
- **More rooms beyond these three.** Each extra station makes every trip less tense. Three stations plus tickets is about the most a 5-minute match can hold.
- **Anything in the cargo bay.** That belongs to the Defender.

## Starting numbers (first guesses, all in Driver.settings)

| Setting | First guess |
|---|---|
| Spare LiOH canisters | 2 |
| Canister slot-in time | 0.5 sec |
| Shutter close window | the full 5-sec flare warning |
| X-flare wobble with shutters closed | 40% of normal |
| Power pool | 100 |
| Thrust power cost | ~4 / sec |
| Power recharge | ~2 / sec |
| Generator power cost | ~8 / sec |
| Generator output | ~1 sec of cab air per sec of good pressure |
