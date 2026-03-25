#include "g_local.h"


/*QUAKED light (0 1 0) (-8 -8 -8) (8 8 8) nonlinear angle negative_spot negative_point q3map_non-dynamic
Non-displayed light.
"light" overrides the default 300 intensity.
Nonlinear checkbox gives inverse square falloff instead of linear
Angle adds light:surface angle calculations (only valid for "Linear" lights) (wolf)
Lights pointed at a target will be spotlights.
"radius" overrides the default 64 unit radius of a spotlight at the target point.
"fade" falloff/radius adjustment value. multiply the run of the slope by "fade" (1.0f default) (only valid for "Linear" lights) (wolf)
"q3map_non-dynamic" specifies that this light should not contribute to the world's 'light grid' and therefore will not light dynamic models in the game.(wolf)
*/
void SP_light(gentity_t* self) {
	self->s.eType = ET_LIGHT;
	VectorCopy(self->dl_color, self->s.light.lightColor);

	sys->LinkEntity(self);
}

/*QUAKED lightJunior (0 0.7 0.3) (-8 -8 -8) (8 8 8) nonlinear angle negative_spot negative_point
Non-displayed light that only affects dynamic game models, but does not contribute to lightmaps
"light" overrides the default 300 intensity.
Nonlinear checkbox gives inverse square falloff instead of linear
Angle adds light:surface angle calculations (only valid for "Linear" lights) (wolf)
Lights pointed at a target will be spotlights.
"radius" overrides the default 64 unit radius of a spotlight at the target point.
"fade" falloff/radius adjustment value. multiply the run of the slope by "fade" (1.0f default) (only valid for "Linear" lights) (wolf)
*/
void SP_lightJunior(gentity_t* self) {
	self->s.eType = ET_LIGHTJUNIOR;
	VectorCopy(self->dl_color, self->s.light.lightColor);

	sys->LinkEntity(self);
}
