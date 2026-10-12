DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012235000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012235000');
-- Add your query below.


-- Vanilla Bonk: the telegraph kit's object (BonkTelegraph). A plain object
-- like the Talent Shard; the run sets its look (display ids 7000-7023 from
-- the client patch tools\telegraphs builds) and its size when it places one.
DELETE FROM `gameobject_template` WHERE `entry` = 900012;
DROP TEMPORARY TABLE IF EXISTS `bonk_telegraph_template`;
CREATE TEMPORARY TABLE `bonk_telegraph_template` AS SELECT * FROM `gameobject_template` WHERE `entry` = 900007;
UPDATE `bonk_telegraph_template` SET `entry` = 900012, `displayId` = 7000, `name` = 'Telegraph', `size` = 1;
INSERT INTO `gameobject_template` SELECT * FROM `bonk_telegraph_template`;
DROP TEMPORARY TABLE `bonk_telegraph_template`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
