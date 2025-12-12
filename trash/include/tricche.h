#ifndef TRICCHE_H
#define TRICCHE_H

#include "tricche_tibia.h"
#include "tricche_loader.h"

#define PLUGINS_MAX 100

typedef struct tricche {

	float         sample_rate;

	tibia_plugin *plugins[PLUGINS_MAX];
	

};

#endif