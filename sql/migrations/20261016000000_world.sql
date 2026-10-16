DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016000000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016000000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: pets get their family's own skills (user-approved, Oct 16).
-- The Cat uses its 1.12 pet skills by itself: Claw (a big hit that puts on
-- Bleed), Bite (a quick one), Dash (speed while its prey is far) and Prowl
-- (it pounces on a new prey far off; its first strike hits hard). Prowl
-- leaves out its stealth look: a stealthed pet is seen by its owner only by
-- stealth detection (Unit::IsVisibleForOrDetect), and one dropped from
-- sight would fade back in over two seconds.
--
-- Kinds (BonkPetAbilityKind): 0 strike (`damage` bites' worth, `stacks` of
-- the family's keyword), 1 dash (`value` ms), 2 pounce (from `value` yd,
-- its strike `damage` bites' worth). Strikes share the spell's damage with
-- the bites (BonkRun::GetPetBiteDamage); a pounce comes on top.
--
-- Call Pet doubles to about an Arcane Shot's damage a second at rank 1
-- (0.6 -> 1.2 of a grunt's health every 1.5 sec, bites and strikes
-- together), and gains +8% Haste as an upgrade.
-- =============================================================

DROP TABLE IF EXISTS `bonk_pet_ability`;
CREATE TABLE `bonk_pet_ability` (
    `family`      TINYINT UNSIGNED NOT NULL,
    `spell_id`    INT UNSIGNED NOT NULL,
    `kind`        TINYINT UNSIGNED NOT NULL,
    `cooldown_ms` INT UNSIGNED NOT NULL,
    `damage`      FLOAT NOT NULL DEFAULT 0,
    `stacks`      TINYINT UNSIGNED NOT NULL DEFAULT 0,
    `value`       FLOAT NOT NULL DEFAULT 0,
    `comment`     VARCHAR(255) NOT NULL DEFAULT '',
    PRIMARY KEY (`family`, `spell_id`, `kind`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Vanilla Bonk: what a pet family does by itself besides biting';

INSERT INTO `bonk_pet_ability` (`family`, `spell_id`, `kind`, `cooldown_ms`, `damage`, `stacks`, `value`, `comment`) VALUES
(1, 16827, 0, 4000, 3,   4, 0,    'Cat: Claw, 3 bites worth, 4 Bleed'),
(1, 17253, 0, 2500, 1.5, 0, 0,    'Cat: Bite, 1.5 bites worth'),
(1, 23099, 1, 8000, 0,   0, 3000, 'Cat: Dash, +40% speed for 3 sec while its prey is over 12 yd off'),
(1, 16827, 2, 8000, 3,   2, 8,    'Cat: Prowl, pounces on a new prey 8 yd or more off; Claw for 3 bites worth and 2 Bleed');

UPDATE `bonk_weapon` SET `damage_fraction` = 1.2,
    `description` = 'Summons your cat to fight for you. It claws, bites, dashes and pounces by itself; its Claw applies Bleed.'
WHERE `id` = 201;
DELETE FROM `bonk_weapon_upgrade` WHERE `weapon_id` = 201 AND `upgrade` = 3;
INSERT INTO `bonk_weapon_upgrade` (`weapon_id`, `upgrade`, `step`) VALUES (201, 3, 8);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
