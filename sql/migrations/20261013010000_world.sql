DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013010000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013010000');
-- Add your query below.


-- Vanilla Bonk: spell damage measured against enemy health. A spell deals
-- damage_fraction of a Grunt's base health per hit (or per tick), so each
-- weapon's damage per second at rank 1 follows one rule per kind of spell,
-- with its cooldown, ticks and extra hits counted:
--   single target   a Grunt dies in about 3 seconds: 1/3 of its health a second
--   extra hits      each more enemy hit (targets, pierce, chain, cleave share) adds half a hit
--   over time       1.2 times that, as the damage arrives late
--   area            35% of a single-target spell's damage per second, per enemy hit
--   control         60% when a spell also stuns, roots, freezes, shields or weakens
--   evolutions      twice the damage of their kind
-- Revenge and Retaliation (fire only after a dodge) and spells without
-- damage of their own keep their values.
UPDATE `bonk_weapon` SET `damage_fraction` = 0.29 WHERE `id` = 1; -- Thunder Clap: 0.5 -> 0.29 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.6 WHERE `id` = 2; -- Throwing Axe: 0.7 -> 0.6 (single control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.087 WHERE `id` = 3; -- Consecration: 0.15 -> 0.087 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.35 WHERE `id` = 5; -- Holy Nova: 0.3 -> 0.35 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.6 WHERE `id` = 6; -- Smite: 0.55 -> 0.6 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.42 WHERE `id` = 7; -- Arcane Shot: 0.5 -> 0.42 (single 2.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.8 WHERE `id` = 8; -- Axe Flurry: 0.6 -> 0.8 (single control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.37 WHERE `id` = 9; -- Throwing Knives: 0.35 -> 0.37 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.6 WHERE `id` = 10; -- Lightning Bolt: 0.55 -> 0.6 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.89 WHERE `id` = 11; -- Chain Lightning: 0.6 -> 0.89 (single 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.41 WHERE `id` = 12; -- Fire Nova: 0.45 -> 0.41 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.53 WHERE `id` = 13; -- Frostbolt: 0.5 -> 0.53 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.43 WHERE `id` = 14; -- Fireball: 0.85 -> 0.43 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.29 WHERE `id` = 15; -- Arcane Explosion: 0.4 -> 0.29 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.56 WHERE `id` = 16; -- Frost Nova: 0.3 -> 0.56 (area control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.6 WHERE `id` = 17; -- Shadow Bolt: 0.55 -> 0.6 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.15 WHERE `id` = 18; -- Corruption: 0.12 -> 0.15 (dot)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.23 WHERE `id` = 19; -- Hellfire: 0.3 -> 0.23 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.57 WHERE `id` = 20; -- Wrath: 0.5 -> 0.57 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.33 WHERE `id` = 21; -- Moonfire: 0.35 -> 0.33 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.19 WHERE `id` = 22; -- Blizzard: 0.3 -> 0.19 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.19 WHERE `id` = 23; -- Rain of Fire: 0.35 -> 0.19 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.21 WHERE `id` = 24; -- Starfall: 0.25 -> 0.21 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.21 WHERE `id` = 25; -- Lightning Cloud: 0.35 -> 0.21 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.12 WHERE `id` = 26; -- Poison Cloud: 0.3 -> 0.12 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.43 WHERE `id` = 30; -- Cleave: 0.55 -> 0.43 (single 1.6 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.12 WHERE `id` = 31; -- Whirlwind: 0.25 -> 0.12 (area spin)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.13 WHERE `id` = 32; -- Rend: 0.25 -> 0.13 (dot 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 1 WHERE `id` = 33; -- Mortal Strike: 1.3 -> 1 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.29 WHERE `id` = 34; -- Sinister Strike: 0.4 -> 0.29 (single 1.4 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.19 WHERE `id` = 35; -- Garrote: 0.2 -> 0.19 (dot)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.17 WHERE `id` = 36; -- Eviscerate: 1.4 -> 1.17 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.56 WHERE `id` = 37; -- Multi-Shot: 0.45 -> 0.56 (single 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.069 WHERE `id` = 38; -- Serpent Sting: 0.15 -> 0.069 (dot 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.42 WHERE `id` = 39; -- Crusader Strike: 0.6 -> 0.42 (single 1.5 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.087 WHERE `id` = 41; -- Shadow Word: Pain: 0.14 -> 0.087 (dot)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.8 WHERE `id` = 42; -- Mind Blast: 1 -> 0.8 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.27 WHERE `id` = 43; -- Flame Shock: 0.4 -> 0.27 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.42 WHERE `id` = 44; -- Stormstrike: 0.55 -> 0.42 (single 1.5 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.05 WHERE `id` = 46; -- Curse of Agony: 0.08 -> 0.05 (dot)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.57 WHERE `id` = 47; -- Searing Pain: 0.6 -> 0.57 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.093 WHERE `id` = 48; -- Insect Swarm: 0.12 -> 0.093 (dot)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.2 WHERE `id` = 103; -- Shadowburn: 0.6 -> 1.2 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.42 WHERE `id` = 104; -- Lightning Wave: 0.6 -> 0.42 (area evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 1 WHERE `id` = 105; -- Starfire: 1.2 -> 1 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.23 WHERE `id` = 106; -- Shockwave: 0.55 -> 0.23 (area evo +alt control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.21 WHERE `id` = 107; -- Poisoned Blades: 0.3 -> 0.21 (area evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.75 WHERE `id` = 108; -- Chimera Shot: 0.55 -> 0.75 (single evo 2.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.5 WHERE `id` = 109; -- Holy Fire: 1 -> 0.5 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.7 WHERE `id` = 110; -- Divine Wrath: 0.45 -> 0.7 (area evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.67 WHERE `id` = 130; -- Execute: 0.6 -> 0.67 (single evo 1.8 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.12 WHERE `id` = 131; -- Bladestorm: 0.22 -> 0.12 (area evo spin)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.23 WHERE `id` = 132; -- Bloodthirst: 0.3 -> 0.23 (dot evo 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.67 WHERE `id` = 133; -- Colossus Smash: 1.4 -> 1.67 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.39 WHERE `id` = 134; -- Blade Flurry: 0.45 -> 0.39 (single evo 1.5 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.2 WHERE `id` = 135; -- Rupture: 0.25 -> 0.2 (dot evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.67 WHERE `id` = 136; -- Assassinate: 1.6 -> 1.67 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.19 WHERE `id` = 137; -- Volley: 0.3 -> 0.19 (area evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.096 WHERE `id` = 138; -- Serpent Spread: 0.18 -> 0.096 (dot evo 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.62 WHERE `id` = 139; -- Divine Storm: 0.65 -> 0.62 (single evo 1.8 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.67 WHERE `id` = 140; -- Holy Barrage: 0.6 -> 0.67 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.12 WHERE `id` = 141; -- Shadow Plague: 0.16 -> 0.12 (dot evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.07 WHERE `id` = 142; -- Mind Shatter: 1.1 -> 1.07 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.33 WHERE `id` = 143; -- Living Flame: 0.45 -> 0.33 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.43 WHERE `id` = 144; -- Windfury: 0.55 -> 0.43 (single evo 1.6 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.2 WHERE `id` = 146; -- Seed of Corruption: 0.14 -> 0.2 (dot evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.067 WHERE `id` = 147; -- Curse of Doom: 0.1 -> 0.067 (dot evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.12 WHERE `id` = 148; -- Locust Swarm: 0.14 -> 0.12 (dot evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.44 WHERE `id` = 160; -- Arcane Missiles: 0.35 -> 0.44 (single 3.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.14 WHERE `id` = 161; -- Flamestrike: 0.6 -> 0.14 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.35 WHERE `id` = 162; -- Cone of Cold: 0.5 -> 0.35 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.2 WHERE `id` = 163; -- Scorch: 0.3 -> 0.2 (single control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.28 WHERE `id` = 170; -- Frozen Orb: 0.45 -> 0.28 (area evo orb~)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.71 WHERE `id` = 171; -- Pyroblast: 0.85 -> 0.71 (single evo alt 1/4)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.36 WHERE `id` = 172; -- Arcane Barrage: 0.35 -> 0.36 (single evo 5.0 hits)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.12 WHERE `id` = 173; -- Ice Barrier: 0.3 -> 1.12 (area evo control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.23 WHERE `id` = 174; -- Absolute Zero: 0.3 -> 0.23 (area evo +alt control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.29 WHERE `id` = 175; -- Arcane Nova: 0.45 -> 0.29 (area evo echo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.17 WHERE `id` = 176; -- Meteor: 0.6 -> 0.17 (area evo +alt)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.42 WHERE `id` = 177; -- Deep Freeze: 0.55 -> 0.42 (area evo control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.27 WHERE `id` = 178; -- Living Bomb: 0.3 -> 0.27 (single evo +alt)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.5 WHERE `id` = 180; -- Heroic Throw: 0.8 -> 0.5 (single control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.5 WHERE `id` = 184; -- Hamstring: 0.35 -> 0.5 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.53 WHERE `id` = 190; -- Thunderous Throw: 0.9 -> 0.53 (single evo 5.0 hits control)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.2 WHERE `id` = 194; -- Piercing Howl: 0.4 -> 1.2 (single evo alt 1/3)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.5 WHERE `id` = 201; -- Call Pet: 0.4 -> 0.5 (single pet)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.15 WHERE `id` = 202; -- Explosive Trap: 0.8 -> 0.15 (area)
UPDATE `bonk_weapon` SET `damage_fraction` = 1 WHERE `id` = 203; -- Aimed Shot: 1.4 -> 1 (single)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.7 WHERE `id` = 204; -- Frost Trap: 0.4 -> 0.7 (area +alt)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.28 WHERE `id` = 206; -- Scatter Shot: 0.5 -> 0.28 (area control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.2 WHERE `id` = 210; -- Rapid Fire: 0.3 -> 0.2 (single evo burst +2)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.8 WHERE `id` = 211; -- Bestial Wrath: 0.5 -> 0.8 (single evo pet)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.23 WHERE `id` = 212; -- Trap Launcher: 0.8 -> 0.23 (area evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 1.67 WHERE `id` = 213; -- Kill Shot: 1.5 -> 1.67 (single evo)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.84 WHERE `id` = 214; -- Freezing Trap: 0.5 -> 0.84 (area evo +alt control)
UPDATE `bonk_weapon` SET `damage_fraction` = 0.49 WHERE `id` = 216; -- Concussive Barrage: 0.6 -> 0.49 (area evo control)

-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
