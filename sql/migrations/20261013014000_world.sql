DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013014000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013014000');
-- Add your query below.


-- Vanilla Bonk: ground spells lasted 1.1 to 1.3 times their cooldown, so
-- several overlapped from the first rank. Each now lasts about 0.8 times
-- its cooldown (whole seconds); Duration ranks still make it longer.
-- damage_fraction is the damage of one tick, so it rises by the ticks lost:
-- damage per enemy over each cooldown stays what 20261013010000 set (same
-- rules, recomputed with the new durations).
UPDATE `bonk_weapon` SET `duration_ms` = 5000, `damage_fraction` = 0.14 WHERE `id` = 3; -- Consecration: 8 s (spell) -> 5 s, 0.087 -> 0.14
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.29 WHERE `id` = 22; -- Blizzard: 6 s -> 4 s, 0.19 -> 0.29
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.29 WHERE `id` = 23; -- Rain of Fire: 6 s -> 4 s, 0.19 -> 0.29
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.32 WHERE `id` = 24; -- Starfall: 6 s -> 4 s, 0.21 -> 0.32
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.32 WHERE `id` = 25; -- Lightning Cloud: 6 s -> 4 s, 0.21 -> 0.32
UPDATE `bonk_weapon` SET `duration_ms` = 8000, `damage_fraction` = 0.15 WHERE `id` = 26; -- Poison Cloud: 10 s -> 8 s, 0.12 -> 0.15
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.29 WHERE `id` = 137; -- Volley: 6 s -> 4 s, 0.19 -> 0.29
UPDATE `bonk_weapon` SET `duration_ms` = 5000, `damage_fraction` = 0.23 WHERE `id` = 161; -- Flamestrike: 8 s (spell) -> 5 s, 0.14 -> 0.23
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.35 WHERE `id` = 174; -- Absolute Zero: 6 s -> 4 s, 0.23 -> 0.35
UPDATE `bonk_weapon` SET `duration_ms` = 5000, `damage_fraction` = 0.23 WHERE `id` = 176; -- Meteor: 8 s (spell) -> 5 s, 0.17 -> 0.23
UPDATE `bonk_weapon` SET `duration_ms` = 4000, `damage_fraction` = 0.19 WHERE `id` = 202; -- Explosive Trap: 6 s -> 4 s, 0.15 -> 0.19
UPDATE `bonk_weapon` SET `duration_ms` = 3000, `damage_fraction` = 0.47 WHERE `id` = 212; -- Trap Launcher: 6 s -> 3 s, 0.23 -> 0.47
-- Frost Trap: its ice patch only slows (its damage is the one hit when it
-- goes off), so only the patch, and the slow of that hit, get shorter.
UPDATE `bonk_weapon` SET `duration_ms` = 5000 WHERE `id` = 204; -- Frost Trap: 8 s -> 5 s, damage unchanged


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
