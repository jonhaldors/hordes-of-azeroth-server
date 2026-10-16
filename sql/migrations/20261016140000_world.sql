DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016140000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016140000');
-- Add your query below.


-- Vanilla Bonk: the run's Hearthstone (Core loop v2, phase D3). Mini-bosses
-- (30%) and now and then an elite (3%) drop one, once a run. Used from the
-- bags it never casts: the server puts an extraction ring at the hero's
-- feet (BonkRun::UseItem); standing in it 10 sec takes the hero home with
-- the loot, epics and legendaries kept. It is lost when the run ends.
-- A copy of the Hearthstone, so the client shows its "Use:" line.
DELETE FROM `item_template` WHERE `entry` = 900230;
DROP TEMPORARY TABLE IF EXISTS `bonk_hearth`;
CREATE TEMPORARY TABLE `bonk_hearth` SELECT * FROM `item_template` WHERE `entry` = 6948
    AND `patch` = (SELECT MAX(`patch`) FROM `item_template` WHERE `entry` = 6948);
UPDATE `bonk_hearth` SET `entry` = 900230, `patch` = 0, `quality` = 2, `bonding` = 1, `buy_price` = 0, `sell_price` = 0,
    `max_count` = 1, `allowable_class` = -1, `allowable_race` = -1, `required_level` = 0,
    `description` = 'Stand in its ring for 10 sec to go home with your loot. Lost when the run ends.';
INSERT INTO `item_template` SELECT * FROM `bonk_hearth`;
DROP TEMPORARY TABLE IF EXISTS `bonk_hearth`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
