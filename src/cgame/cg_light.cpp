// cg_light.cpp
//

#include "cg_local.h"

void CG_Light(centity_t* cent) {
	entityState_t *s1;

	s1 = &cent->currentState;
	sys->R_AddLightToScene(cent->lerpOrigin, s1->light.lightRadius, s1->light.lightColor[0], s1->light.lightColor[1], s1->light.lightColor[2], 0);
}

void CG_LightJunior(centity_t* cent) {

}