#ifndef HARD_PATHS_HPP
#define HARD_PATHS_HPP

#define HARDPATH_DATA_PREFIX		"data/"
#define HARDPATH_USR_PREFIX			"user/"

#define HARDPATH_SHADER_PREFIX		HARDPATH_DATA_PREFIX"shaders/"
#define HARDPATH_DEMO_TEMPLATE		HARDPATH_USR_PREFIX"replay_{}.ratdemo" // not used in main.cpp

#define HARDPATH_LOGFILE			HARDPATH_USR_PREFIX"app.log"
#define HARDPATH_SETTINGS_USER		HARDPATH_USR_PREFIX"settings.cfg"
#define HARDPATH_SETTINGS_DEFAULT	HARDPATH_DATA_PREFIX"settings.cfg.default"
#define HARDPATH_APP_ICON			HARDPATH_DATA_PREFIX"icon.png"
#define HARDPATH_KEYBINDS			HARDPATH_USR_PREFIX"keybinds.cfg"

#define HARDPATH_MODELS				HARDPATH_DATA_PREFIX"models.svg"
#define HARDPATH_EXPLOSION_IMG		HARDPATH_DATA_PREFIX"explosion_wave.png"
#define HARDPATH_CROSSHAIR_IMG		HARDPATH_DATA_PREFIX"crosshair.png"



#include <string>

// compiler + OS + architecture
std::string get_full_platform_version();

#endif // HARD_PATHS_HPP
