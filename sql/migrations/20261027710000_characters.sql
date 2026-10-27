INSERT INTO `bonk_account_talent` (`account`, `talent`, `rank`, `enabled`)
SELECT `account`, 18, LEAST(`rank`, 5), 0 FROM `bonk_account_upgrade` WHERE `upgrade` = 29
ON DUPLICATE KEY UPDATE `rank` = GREATEST(`bonk_account_talent`.`rank`, VALUES(`rank`));
DELETE FROM `bonk_account_upgrade` WHERE `upgrade` = 29;
