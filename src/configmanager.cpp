#include "configmanager.h"

#include "addonmanager.h"
#include "configs/webconfig.h"
#include "addons/neopicoleds.h"

void ConfigManager::setup(ConfigType config) {
	switch(config) {
		case CONFIG_TYPE_WEB:
			setupConfig(new WebConfig());
			break;
	}
	this->cType = config;
}

void ConfigManager::loop() {
	config->loop();
}

void ConfigManager::setupConfig(GPConfig * gpconfig) {
	gpconfig->setup();
	this->config = gpconfig;
}

void ConfigManager::setGamepadOptions(Gamepad* gamepad) {
	gamepad->save();
}

void ConfigManager::setLedOptions(LEDOptions ledOptions) {
	Storage::getInstance().setLEDOptions(ledOptions);
}

void ConfigManager::setBoardOptions(BoardOptions boardOptions) {
	Storage::getInstance().setBoardOptions(boardOptions);
	GamepadStore.save();
}

void ConfigManager::setPreviewBoardOptions(BoardOptions boardOptions) {
	Storage::getInstance().setPreviewBoardOptions(boardOptions);
}

void ConfigManager::setSplashImage(SplashImage image) {
	Storage::getInstance().setSplashImage(image);
}
