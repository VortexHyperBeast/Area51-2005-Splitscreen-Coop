# Area 51 Splitscreen Co-op: changes per official version

Each section is the description of that version's official release (the "official" job of
`.github/workflows/coop-build.yml` copies the section whose heading is the release name).
Newest first.

## v0.95.55_Harder_Co-op

- **New: Co-op Settings** in the main menu (in place of Online). Change a row with A or left/right; it is saved at once and used right away, for every co-op save:
  - **Difficulty:** Easy (enemies and bosses as in single player), **Normal** (1.5x health, the default), Hard (2x health)
  - Downed and revive on/off, bleed-out time, revive time, revive health
  - Controllers normal or swapped
- **Fixed:** the objective marker was only shown to one player; both players see it now.
- **Changed:** keyboard "join partner" (teleport to the other player) moved from Tab to **T**. Tab is only Use now, so using something no longer teleports you. Existing profiles are switched automatically.

## v0.95.49_Harder_Co-op

- **New: harder co-op.** In Splitscreen Co-op, enemies and bosses take 1.5x as many hits as in single player, to balance two players. Single Player is unchanged.
- Setting `enemy_health_percent` in `a51_coop.cfg` (100 = as in single player, 150 = default).

## v0.95.26_Original

The first full version of the mod: the whole campaign for two players on one PC.

- **Main menu:** Single Player and Splitscreen Co-op, each with its own saves (co-op uses the profiles "Splitscreen P1" and "Splitscreen P2").
- **Split screen:** top and bottom view, one controller each, each player with their own HUD, settings and body.
- **Co-op rules:** a player with no health goes down and the partner revives them by holding USE; weapons, ammo and unlocks are shared, health is separate; mutation works for both players.
- **Level Select** starts any mission of the campaign in co-op.
- **Pause menu:** swap the controllers and choose each player's body.
- **Fixed:** freeze with the blue gun (Meson Cannon), crash when starting The Last Exit, The Last Exit train leaving without the players, crashes and falling through the floor in long sessions, Blaze.
- Darker hazmat faces for the players, like the NPCs.
- Start screen, version and thank-you text in the main menu.
