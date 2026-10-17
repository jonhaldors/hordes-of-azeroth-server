DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016490000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016490000');
-- Add your query below.


-- Vanilla Bonk: description columns of 128 characters cut longer text off
-- without a word (the server runs non-strict); 255 like bonk_card's.
ALTER TABLE `bonk_item_set_bonus` MODIFY `description` VARCHAR(255) NOT NULL;
ALTER TABLE `bonk_passive` MODIFY `description` VARCHAR(255) NOT NULL DEFAULT '';
ALTER TABLE `bonk_talent` MODIFY `description` VARCHAR(255) NOT NULL DEFAULT '';
ALTER TABLE `bonk_upgrade` MODIFY `description` VARCHAR(255) NOT NULL DEFAULT '';
ALTER TABLE `bonk_weapon` MODIFY `description` VARCHAR(255) NOT NULL DEFAULT '';
-- Frost Trap's, cut at "take 5% more".
UPDATE `bonk_weapon` SET `description` = 'Places a frost trap that goes off when an enemy comes close. For 6 sec, normal enemies inside are slowed by 60% and take 5% more damage.' WHERE `id` = 204;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
