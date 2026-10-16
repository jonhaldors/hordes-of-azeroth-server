DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016220000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016220000');
-- Add your query below.


-- Vanilla Bonk: Molten Core's areas round a boss or mini-boss reached 45
-- yards with no warning (Panic, Magma Shackles, Impending Doom, Gehennas'
-- Curse). Every area round a run enemy, crowd control too, now waits under
-- a red circle of its reach, at most 15 yards (BonkMobAI::StartArea): these
-- come down to 10. Radius index 13 is 10 yards; -1 leaves a field as it is.
INSERT INTO `spell_effect_mod` (`Id`, `EffectIndex`, `Effect`, `EffectDieSides`, `EffectBaseDice`, `EffectDicePerLevel`, `EffectRealPointsPerLevel`,
    `EffectBasePoints`, `EffectAmplitude`, `EffectPointsPerComboPoint`, `EffectChainTarget`, `EffectMultipleValue`, `EffectMechanic`,
    `EffectImplicitTargetA`, `EffectImplicitTargetB`, `EffectRadiusIndex`, `EffectApplyAuraName`, `EffectItemType`, `EffectMiscValue`,
    `EffectTriggerSpell`, `Comment`) VALUES
(19408, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, 'Vanilla Bonk: Panic 10 yards (was 45)'),
(19496, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, 'Vanilla Bonk: Magma Shackles 10 yards (was 45)'),
(19702, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, 'Vanilla Bonk: Impending Doom 10 yards (was 45)'),
(19716, 0, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 13, -1, -1, -1, -1, 'Vanilla Bonk: Gehennas'' Curse 10 yards (was 45)')
ON DUPLICATE KEY UPDATE `EffectRadiusIndex` = VALUES(`EffectRadiusIndex`);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
