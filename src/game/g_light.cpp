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

	float fade;
	float linearScale = 1.0f;
	G_SpawnFloat("fade", "1", &fade);

	self->s.light.lightRadius = (self->s.light.lightRadius * linearScale) / max(fade, 0.001f);

	if (VectorLength(self->dl_color) == 0)
	{
		VectorSet(self->dl_color, 1, 1, 1, 1);
	}

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
