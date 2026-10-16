DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016230000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016230000');
-- Add your query below.


-- Vanilla Bonk, Phase F: unlock prices rise with each unlock paid for
-- (BonkQuests::GetUnlockPrice). The ones kept from before phase E were not
-- paid for and don't count.
ALTER TABLE `bonk_account_unlock` ADD COLUMN IF NOT EXISTS `paid` TINYINT UNSIGNED NOT NULL DEFAULT 1;
UPDATE `bonk_account_unlock` SET `paid` = 0;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
