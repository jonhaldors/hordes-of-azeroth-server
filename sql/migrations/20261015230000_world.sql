DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015230000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015230000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: Hunter v2, second playtest (user-approved).
--
-- Bestial Wrath becomes Pack Leader: one spirit wolf (creature 900115,
-- Ghost Wolf's look) fights for the hero for the whole run, biting like the
-- Cat. Every 15 sec it howls, and the hero's other pets (never the wolf)
-- frenzy for 6 sec: double damage, and bites that strike every enemy in
-- front. Count adds no wolves. Upgrades: its own damage, frenzy damage
-- (upgrade 11), howl duration, Haste (howls more often).
--
-- Frost Trap: the real Frost Trap Aura's 60% slow, while inside the ice,
-- on normal enemies only (weapon flag 0x40; elites, mini-bosses and bosses
-- walk through until an evolution says otherwise). No damage and no Chill.
-- Slowed enemies take 5% more damage, grown by its ranks like the Mark;
-- upgrade 10 adds 3% a step, up to 15%.
--
-- Hunter's Mark: no flare on the ground (it burned as long as the Mark);
-- the Mark's look on the enemies shows it.
-- =============================================================

UPDATE `bonk_weapon` SET
    `name` = 'Pack Leader', `icon` = 'Spell_Nature_SpiritWolf',
    `description` = 'A spirit wolf fights for you. Every 15 sec it howls: your other pets frenzy for 6 sec, dealing double damage to all in front.',
    `damage_fraction` = 0.6, `tick_ms` = 1500, `cooldown_ms` = 15000, `duration_ms` = 6000, `range` = 0
WHERE `id` = 211;
DELETE FROM `bonk_weapon_upgrade` WHERE `weapon_id` = 211;
INSERT INTO `bonk_weapon_upgrade` (`weapon_id`, `upgrade`, `step`) VALUES
(211, 7, 15),       -- damage dealt by its wolf
(211, 11, 15),      -- damage dealt by frenzied pets
(211, 5, 15),       -- howl duration
(211, 3, 8);        -- Haste: howls more often
UPDATE `creature_template` SET `name` = 'Pack Leader' WHERE `entry` = 900115;

UPDATE `bonk_weapon` SET
    `description` = 'Throws a frost trap at the largest group. For 6 sec, normal enemies inside are slowed by 60% and take 5% more damage.',
    `damage_fraction` = 0, `tick_ms` = 0, `applies` = 0, `apply_stacks` = 0, `flags` = `flags` | 0x40
WHERE `id` = 204;
DELETE FROM `bonk_weapon_upgrade` WHERE `weapon_id` = 204;
INSERT INTO `bonk_weapon_upgrade` (`weapon_id`, `upgrade`, `step`) VALUES
(204, 10, 3),       -- damage taken by enemies it slows
(204, 5, 15),       -- duration
(204, 3, 8);        -- Haste
-- The field's own slow is back (its trigger stays a dummy).
DELETE FROM `spell_effect_mod` WHERE `Id` = 13810 AND `EffectIndex` = 0;

UPDATE `bonk_weapon` SET
    `description` = 'Marks every enemy within 10 yards of the largest group for 8 sec. Marked enemies take 15% more damage.'
WHERE `id` = 205;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
